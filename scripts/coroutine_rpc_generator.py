#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# coroutine_rpc 示例脚手架生成器
#
# 参考 tinyrpc/generator/tinyrpc_generator.py 的设计：
#   * python3 标准库 string.Template + templates/ 目录
#   * 从输入 proto 轻量解析 package/service/rpc 方法
#   * 业务模板 = 仓库 example/ 下的官方示例本体（随 --demo 选择）
#   * 任意 proto 无法匹配官方 schema 时降级为可编译的通用骨架
#
# 用法：
#   python3 scripts/coroutine_rpc_generator.py rpc  -i proto/user.proto  -n demo -o ./
#   python3 scripts/coroutine_rpc_generator.py http --servlet-path /hello --port 8080
#   python3 scripts/coroutine_rpc_generator.py async_http -i proto/async_http_example.proto
#   python3 scripts/coroutine_rpc_generator.py -h

import os
import re
import shutil
import sys
from argparse import ArgumentParser
from datetime import datetime
from string import Template

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
TEMPLATE_DIR = os.path.join(SCRIPT_DIR, "templates")
REPO_ROOT = os.path.normpath(os.path.join(SCRIPT_DIR, ".."))


def banner(kind):
    return "=" * 90


def now():
    return datetime.now().strftime("%Y-%m-%d %H:%M:%S")


def read_template(rel_path):
    path = os.path.join(TEMPLATE_DIR, rel_path)
    with open(path, "r", encoding="utf-8") as f:
        return f.read()


def render(rel_path, mapping, residual_check=True):
    text = Template(read_template(rel_path)).safe_substitute(mapping)
    if residual_check:
        rest = re.findall(r"\$\{([A-Za-z_][A-Za-z0-9_]*)\}", text)
        if rest:
            uniq = sorted(set(rest))
            raise RuntimeError(
                "template {} has un-substituted tokens: {}".format(
                    rel_path, uniq))
    return text


def posix_path(path):
    return path.replace("\\", "/")


# ---------------------------------------------------------------- proto parse
def strip_comments(text):
    text = re.sub(r"//[^\n]*", "", text)
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return text


def parse_proto(path):
    """Return (package, [ {name, methods:[{name, req, resp}]} ])."""
    with open(path, "r", encoding="utf-8", errors="ignore") as f:
        text = strip_comments(f.read())

    pm = re.search(r"^\s*package\s+([A-Za-z_][\w.]*)\s*;", text, re.M)
    pkg = pm.group(1) if pm else ""

    services = []
    pos = 0
    n = len(text)
    while pos < n:
        m = re.search(r"^\s*service\s+([A-Za-z_]\w*)\s*\{", text[pos:], re.M)
        if not m:
            break
        svc_name = m.group(1)
        start = pos + m.end() - 1  # index of '{'
        depth = 0
        i = start
        while i < n:
            if text[i] == "{":
                depth += 1
            elif text[i] == "}":
                depth -= 1
                if depth == 0:
                    break
            i += 1
        body = text[start:i]
        methods = []
        for mm in re.finditer(
                r"\brpc\s+([A-Za-z_]\w*)\s*\(\s*([A-Za-z_.][\w.]*)\s*\)\s+"
                r"returns\s*\(\s*([A-Za-z_.][\w.]*)\s*\)\s*;", body):
            methods.append({"name": mm.group(1), "req": mm.group(2),
                            "resp": mm.group(3)})
        services.append({"name": svc_name, "methods": methods})
        pos = i + 1
    return pkg, services


def cpp_qual(pkg, t):
    """Map a proto type name to a C++ namespace-qualified name."""
    if t.startswith("."):
        t = t[1:]
    if "." in t:
        return "::" + t.replace(".", "::")
    if pkg:
        return pkg + "::" + t
    return "::" + t


def method_map_tokens(pkg, methods):
    mapping = {}
    for m in methods:
        u = m["name"].upper()
        mapping["REQ_" + u + "_T"] = cpp_qual(pkg, m["req"])
        mapping["RESP_" + u + "_T"] = cpp_qual(pkg, m["resp"])
    return mapping


# ---------------------------------------------------------------- code blocks
def block_proto_cmake(name):
    return (
        "find_program(GEN_PROTOC_EXECUTABLE protoc)\n"
        "if(NOT GEN_PROTOC_EXECUTABLE)\n"
        "  message(FATAL_ERROR \"protoc is required to build this example\")\n"
        "endif()\n"
        "\n"
        "set(GEN_PROTO_FILE ${CMAKE_CURRENT_SOURCE_DIR}/proto/" + name + ".proto)\n"
        "set(GEN_PROTO_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)\n"
        "set(GEN_PROTO_SOURCE ${GEN_PROTO_OUTPUT_DIR}/" + name + ".pb.cc)\n"
        "set(GEN_PROTO_HEADER ${GEN_PROTO_OUTPUT_DIR}/" + name + ".pb.h)\n"
        "\n"
        "add_custom_command(\n"
        "  OUTPUT ${GEN_PROTO_SOURCE} ${GEN_PROTO_HEADER}\n"
        "  COMMAND ${CMAKE_COMMAND} -E make_directory ${GEN_PROTO_OUTPUT_DIR}\n"
        "  COMMAND ${GEN_PROTOC_EXECUTABLE}\n"
        "          --proto_path=${CMAKE_CURRENT_SOURCE_DIR}/proto\n"
        "          --cpp_out=${GEN_PROTO_OUTPUT_DIR}\n"
        "          ${GEN_PROTO_FILE}\n"
        "  DEPENDS ${GEN_PROTO_FILE}\n"
        "  VERBATIM\n"
        ")\n"
        "\n"
        "add_library(" + name + "_proto STATIC ${GEN_PROTO_SOURCE} ${GEN_PROTO_HEADER})\n"
        "target_include_directories(" + name + "_proto PUBLIC ${GEN_PROTO_OUTPUT_DIR})\n"
        "target_link_libraries(" + name + "_proto protobuf)\n")


def block_provider_generic_overrides(pkg, svc, methods):
    lines = []
    for m in methods:
        req = cpp_qual(pkg, m["req"])
        resp = cpp_qual(pkg, m["resp"])
        lines.append(
            "  void " + m["name"] + "(::google::protobuf::RpcController* controller,\n"
            "                 const " + req + "* request,\n"
            "                 " + resp + "* response,\n"
            "                 ::google::protobuf::Closure* done) override {\n"
            "    (void)controller;\n"
            "    (void)request;\n"
            "    (void)response;\n"
            "    AppInfoLog(\"" + svc + "." + m["name"] + " called (skeleton, TODO business)\");\n"
            "    done->Run();\n"
            "  }\n")
    return "\n".join(lines)


def block_consumer_generic_calls(pkg, methods):
    lines = []
    for m in methods:
        req = cpp_qual(pkg, m["req"])
        resp = cpp_qual(pkg, m["resp"])
        lines.append(
            "  {\n"
            "    " + req + " req;\n"
            "    " + resp + " rsp;\n"
            "    RpcController controller;\n"
            "    stub." + m["name"] + "(&controller, &req, &rsp, nullptr);\n"
            "    if (controller.Failed()) {\n"
            "      std::cout << \"rpc " + m["name"] + " failed: \" << controller.ErrorText() << std::endl;\n"
            "    } else {\n"
            "      std::cout << \"rpc " + m["name"] + " success\" << std::endl;\n"
            "    }\n"
            "  }\n")
    return "\n".join(lines)


def block_backend_generic_overrides(pkg, svc, methods):
    lines = []
    for m in methods:
        req = cpp_qual(pkg, m["req"])
        resp = cpp_qual(pkg, m["resp"])
        lines.append(
            "  void " + m["name"] + "(google::protobuf::RpcController* controller,\n"
            "                 const " + req + "* request,\n"
            "                 " + resp + "* response,\n"
            "                 google::protobuf::Closure* done) override {\n"
            "    (void)controller;\n"
            "    (void)request;\n"
            "    (void)response;\n"
            "    AppInfoLog(\"" + svc + "." + m["name"] + " finished (skeleton, TODO business)\");\n"
            "    sleep(1);\n"
            "    if (done != nullptr) {\n"
            "      done->Run();\n"
            "    }\n"
            "  }\n")
    return "\n".join(lines)


def servlet_class_from_path(path):
    parts = [p for p in re.split(r"[^A-Za-z0-9]+", path.strip("/")) if p]
    if not parts:
        return "Root"
    return "".join(p[0].upper() + p[1:] for p in parts)


def block_http_servlet_classes(servlet_paths):
    blocks = []
    for path in servlet_paths:
        cls = servlet_class_from_path(path) + "Servlet"
        if path == "/echo":
            body = (
                "    setHttpCode(response, crpc::HTTP_OK);\n"
                "    setHttpContentType(response, \"text/plain;charset=utf-8\");\n"
                "    setHttpBody(response, request->m_request_body);\n")
        else:
            body = (
                "    setHttpCode(response, crpc::HTTP_OK);\n"
                "    setHttpContentType(response, \"text/plain;charset=utf-8\");\n"
                "\n"
                "    std::string name = \"world\";\n"
                "    const auto it = request->m_query_maps.find(\"name\");\n"
                "    if (it != request->m_query_maps.end() && !it->second.empty()) {\n"
                "      name = it->second;\n"
                "    }\n"
                "    setHttpBody(response, \"hello \" + name + \"\\n\");\n")
        blocks.append(
            "class " + cls + " : public crpc::HttpServlet {\n"
            " public:\n"
            "  void handle(crpc::HttpRequest* request,\n"
            "              crpc::HttpResponse* response) override {\n"
            + body +
            "  }\n"
            "\n"
            "  std::string getServletName() override {\n"
            "    return \"" + cls + "\";\n"
            "  }\n"
            "};\n")
    return "\n".join(blocks)


def block_http_register_lines(servlet_paths):
    lines = []
    for path in servlet_paths:
        cls = servlet_class_from_path(path) + "Servlet"
        lines.append(
            "  if (server->registerHttpServlet(\n"
            "          \"" + path + "\", std::make_shared<" + cls + ">())) {\n"
            "    InfoLog << \"event=servlet_registered path=" + path + "\";\n"
            "  } else {\n"
            "    register_ok = false;\n"
            "  }")
    return "\n".join(lines)


# ---------------------------------------------------------------- mode helpers
def choose_rpc_demo(methods, opt):
    names = set(m["name"] for m in methods)
    if opt in ("generic", "none"):
        return "generic"
    if opt == "user":
        if not {"Login", "Register"} <= names:
            raise RuntimeError(
                "--demo user requires a service with Login and Register rpcs")
        return "user"
    if opt == "friend":
        if "GetFriendsList" not in names:
            raise RuntimeError(
                "--demo friend requires a service with a GetFriendsList rpc")
        return "friend"
    # auto
    if {"Login", "Register"} <= names:
        return "user"
    if "GetFriendsList" in names:
        return "friend"
    return "generic"


def choose_async_demo(methods, opt):
    names = set(m["name"] for m in methods)
    if opt in ("generic", "none"):
        return "generic"
    if opt == "queryage":
        if "QueryAge" not in names:
            raise RuntimeError(
                "--demo queryage requires a service with a QueryAge rpc")
        return "queryage"
    if "QueryAge" in names:
        return "queryage"
    return "generic"


def write_if(force, path, content, executable=False):
    if os.path.exists(path) and not force:
        print("file exist, skip: " + path)
        return
    write_always(path, content, executable)


def write_always(path, content, executable=False):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(content)
    if executable:
        try:
            os.chmod(path, 0o755)
        except OSError:
            pass
    print("succ write to " + path)


def copy_proto(src, dst):
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    if os.path.exists(dst):
        os.remove(dst)
    shutil.copyfile(src, dst)
    print("succ write to " + dst)


def ensure_build_dir(proj_dir):
    build_dir = os.path.join(proj_dir, "build")
    if os.path.exists(build_dir):
        print("build dir exist, skip: " + build_dir)
    else:
        os.makedirs(build_dir, exist_ok=True)
        print("succ make dir in " + build_dir)


def conf_cases_for(project, conf_map):
    lines = []
    for exe, conf in sorted(conf_map.items()):
        lines.append(
            "    " + exe + ") echo \"${PROJECT_ROOT}/conf/" + conf + "\" ;;")
    return "\n".join(lines)


def write_conf(force, proj_dir, project, conf_name, port, zk_port):
    stem = conf_name[:-len(".conf")]
    log_prefix = stem if stem.startswith(project + "_") else project
    log_path = posix_path(os.path.abspath(os.path.join(
        proj_dir, "build", "bin", "log")))
    mapping = {
        "CREATE_TIME": now(),
        "CONF_PORT": str(port),
        "ZK_PORT": str(zk_port),
        "LOG_PATH": log_path,
        "LOG_PREFIX": log_prefix,
        "IO_THREAD_NUM": "4",
    }
    content = render(os.path.join("common", "conf.ini.template"), mapping)
    write_if(force, os.path.join(proj_dir, "conf", conf_name), content)


def write_shell_scripts(force, proj_dir, project, procs, conf_map):
    mapping = {
        "CREATE_TIME": now(),
        "PROJECT_NAME": project,
        "PROCS": " ".join(procs),
        "CONF_CASES": conf_cases_for(project, conf_map),
    }
    for name in ("run.sh", "shutdown.sh"):
        content = render(os.path.join("common", name + ".template"), mapping,
                         residual_check=False)
        write_if(force, os.path.join(proj_dir, "bin", name), content,
                 executable=True)


def write_cmake(proj_dir, project, proto_cmake, targets):
    mapping = {
        "CREATE_TIME": now(),
        "PROJECT_NAME": project,
        "SRC_ROOT_DEFAULT": posix_path(REPO_ROOT),
        "PROTO_CMAKE": proto_cmake,
        "EXAMPLE_TARGETS": targets,
    }
    content = render(os.path.join("common", "CMakeLists.txt.template"), mapping,
                     residual_check=False)
    write_always(os.path.join(proj_dir, "CMakeLists.txt"), content)


def write_project_readme(proj_dir, project, kind, text):
    header = (
        "# " + project + " (" + kind + " example)\n\n"
        "Generated by coroutine_rpc coroutine_rpc_generator.py.\n\n")
    write_always(os.path.join(proj_dir, "README.md"), header + text)


# -------------------------------------------------------------------- readers
def readme_rpc(project):
    return (
        "## 构建\n\n"
        "```bash\n"
        "cmake -B build -DMPRPC_ROOT=<coroutine_rpc 仓库路径>\n"
        "cmake --build build -j$(nproc)\n"
        "```\n\n"
        "说明：MPRPC_ROOT 默认已指向生成时所在仓库，如仓库路径变化可用 -D 覆盖。\n\n"
        "## 运行（需先启动 ZooKeeper）\n\n"
        "两种方式任选其一：\n\n"
        "方式一：脚本一键启停\n"
        "```bash\n"
        "sh bin/run.sh provider\n"
        "sh bin/run.sh consumer\n"
        "sh bin/shutdown.sh\n"
        "```\n\n"
        "方式二：手动运行\n"
        "```bash\n"
        "cd " + project + "\n"
        "./build/bin/provider -i conf/" + project + ".conf\n"
        "./build/bin/consumer -i conf/" + project + ".conf\n"
        "```\n\n"
        "consumer 控制台会打印各 RPC 方法的调用结果。\n")


def readme_http(project):
    return (
        "## 构建\n\n"
        "```bash\n"
        "cmake -B build -DMPRPC_ROOT=<coroutine_rpc 仓库路径>\n"
        "cmake --build build -j$(nproc)\n"
        "```\n\n"
        "说明：MPRPC_ROOT 默认已指向生成时所在仓库，如仓库路径变化可用 -D 覆盖。\n\n"
        "## 运行（纯 HTTP，无需 ZooKeeper）\n\n"
        "方式一：脚本\n"
        "```bash\n"
        "sh bin/run.sh\n"
        "sh bin/shutdown.sh\n"
        "```\n\n"
        "方式二：手动\n"
        "```bash\n"
        "./build/bin/http_server -i conf/" + project + ".conf\n"
        "```\n\n"
        "监听端口为 conf/" + project + ".conf 里的 rpcserverport。\n")


def readme_async_http(project, server_port):
    return (
        "## 构建\n\n"
        "```bash\n"
        "cmake -B build -DMPRPC_ROOT=<coroutine_rpc 仓库路径>\n"
        "cmake --build build -j$(nproc)\n"
        "```\n\n"
        "说明：MPRPC_ROOT 默认已指向生成时所在仓库，如仓库路径变化可用 -D 覆盖。\n\n"
        "## 运行（需先启动 ZooKeeper）\n\n"
        "方式一：脚本一键启动 backend 与 server\n"
        "```bash\n"
        "sh bin/run.sh\n"
        "sh bin/shutdown.sh\n"
        "```\n\n"
        "方式二：手动\n"
        "```bash\n"
        "./build/bin/async_http_backend -i conf/" + project + "_backend.conf\n"
        "./build/bin/async_http_server  -i conf/" + project + "_server.conf\n"
        "```\n\n"
        "## 验证\n\n"
        "```bash\n"
        "curl 'http://127.0.0.1:" + str(server_port) + "/qps?id=1'\n"
        "curl 'http://127.0.0.1:" + str(server_port) + "/block?id=1'\n"
        "curl 'http://127.0.0.1:" + str(server_port) + "/nonblock?id=1'\n"
        "```\n\n"
        "server 监听端口见 conf/" + project + "_server.conf 的 rpcserverport，\n"
        "backend 监听端口见 conf/" + project + "_backend.conf 的 rpcserverport。\n")


# ----------------------------------------------------------------------- rpc
def gen_rpc(args):
    if not args.proto:
        raise RuntimeError("rpc 模式必须提供 -i/--proto 文件")
    pkg, services = parse_proto(args.proto)
    if not services:
        raise RuntimeError("未在 proto 中找到 service，请检查 proto 文件")
    svc = services[0]
    name = args.name or os.path.splitext(os.path.basename(args.proto))[0]
    proj_dir = os.path.join(args.output, name)
    os.makedirs(proj_dir, exist_ok=True)
    ensure_build_dir(proj_dir)

    proto_basename = os.path.splitext(os.path.basename(args.proto))[0]
    demo = choose_rpc_demo(svc["methods"], args.demo)
    print("use demo profile: " + demo)

    print(banner("="))
    print("copy proto file")
    dst_proto = os.path.join(proj_dir, "proto", proto_basename + ".proto")
    copy_proto(args.proto, dst_proto)

    base_map = {
        "CREATE_TIME": now(),
        "PROTO_BASENAME": proto_basename,
        "SVC": svc["name"],
        "SVCFULL": cpp_qual(pkg, svc["name"]),
    }
    base_map.update(method_map_tokens(pkg, svc["methods"]))

    src_dir = os.path.join(proj_dir, "src")
    if demo == "generic":
        base_map["OVERRIDES"] = block_provider_generic_overrides(
            pkg, svc["name"], svc["methods"])
        base_map["METHOD_CALLS"] = block_consumer_generic_calls(
            pkg, svc["methods"])
        provider_tpl = os.path.join("rpc", "provider_generic.cc.template")
        consumer_tpl = os.path.join("rpc", "consumer_generic.cc.template")
    else:
        provider_tpl = os.path.join("rpc", "provider_" + demo + ".cc.template")
        consumer_tpl = os.path.join("rpc", "consumer_" + demo + ".cc.template")

    write_if(args.force, os.path.join(src_dir, "provider.cc"),
             render(provider_tpl, base_map))
    write_if(args.force, os.path.join(src_dir, "consumer.cc"),
             render(consumer_tpl, base_map))

    write_conf(args.force, proj_dir, name, name + ".conf",
               args.provider_port, args.zk_port)

    proto_cmake = block_proto_cmake(proto_basename)
    targets = (
        "add_executable(provider src/provider.cc)\n"
        "target_link_libraries(provider mprpc " + proto_basename + "_proto protobuf)\n"
        "\n"
        "add_executable(consumer src/consumer.cc)\n"
        "target_link_libraries(consumer mprpc " + proto_basename + "_proto protobuf)\n")
    write_cmake(proj_dir, name, proto_cmake, targets)

    write_shell_scripts(args.force, proj_dir, name,
                        ["provider", "consumer"], {})
    write_project_readme(proj_dir, name, "rpc", readme_rpc(name))
    return proj_dir


# ---------------------------------------------------------------------- http
def gen_http(args):
    name = args.name or "demo"
    proj_dir = os.path.join(args.output, name)
    os.makedirs(proj_dir, exist_ok=True)
    ensure_build_dir(proj_dir)

    paths = args.servlet_path or ["/hello", "/echo"]
    mapping = {
        "CREATE_TIME": now(),
        "SERVLET_CLASSES": block_http_servlet_classes(paths),
        "REGISTER_LINES": block_http_register_lines(paths),
    }
    write_if(args.force, os.path.join(proj_dir, "src", "http_server.cc"),
             render(os.path.join("http", "http_server.cc.template"), mapping))

    write_conf(args.force, proj_dir, name, name + ".conf",
               args.port, args.zk_port)

    proto_cmake = ""
    targets = (
        "add_executable(http_server src/http_server.cc)\n"
        "target_link_libraries(http_server mprpc)\n")
    write_cmake(proj_dir, name, proto_cmake, targets)

    write_shell_scripts(args.force, proj_dir, name,
                        ["http_server"], {})
    write_project_readme(proj_dir, name, "http", readme_http(name))
    return proj_dir


# ------------------------------------------------------------------ async_http
def gen_async_http(args):
    if not args.proto:
        raise RuntimeError("async_http 模式必须提供 -i/--proto 文件")
    pkg, services = parse_proto(args.proto)
    if not services:
        raise RuntimeError("未在 proto 中找到 service，请检查 proto 文件")
    svc = services[0]
    name = args.name or os.path.splitext(os.path.basename(args.proto))[0]
    proj_dir = os.path.join(args.output, name)
    os.makedirs(proj_dir, exist_ok=True)
    ensure_build_dir(proj_dir)

    proto_basename = os.path.splitext(os.path.basename(args.proto))[0]
    if len(svc["methods"]) == 0:
        raise RuntimeError("service {} has no rpc method".format(svc["name"]))
    method = svc["methods"][0]
    demo = choose_async_demo(svc["methods"], args.demo)
    print("use demo profile: " + demo)

    print(banner("="))
    print("copy proto file")
    dst_proto = os.path.join(proj_dir, "proto", proto_basename + ".proto")
    copy_proto(args.proto, dst_proto)

    base_map = {
        "CREATE_TIME": now(),
        "PROTO_BASENAME": proto_basename,
        "SVC": svc["name"],
        "SVCFULL": cpp_qual(pkg, svc["name"]),
        "SVC_STUB": cpp_qual(pkg, svc["name"]) + "_Stub",
        "METHOD": method["name"],
        "REQ_T": cpp_qual(pkg, method["req"]),
        "RESP_T": cpp_qual(pkg, method["resp"]),
    }

    src_dir = os.path.join(proj_dir, "src")
    if demo == "generic":
        base_map["OVERRIDES"] = block_backend_generic_overrides(
            pkg, svc["name"], svc["methods"])
        backend_tpl = os.path.join("async_http", "backend_generic.cc.template")
        server_tpl = os.path.join("async_http", "server_generic.cc.template")
    else:
        backend_tpl = os.path.join("async_http", "backend_queryage.cc.template")
        server_tpl = os.path.join("async_http", "server_queryage.cc.template")

    write_if(args.force, os.path.join(src_dir, "backend.cc"),
             render(backend_tpl, base_map))
    write_if(args.force, os.path.join(src_dir, "server.cc"),
             render(server_tpl, base_map))

    write_conf(args.force, proj_dir, name, name + "_backend.conf",
               args.backend_port, args.zk_port)
    write_conf(args.force, proj_dir, name, name + "_server.conf",
               args.server_port, args.zk_port)

    proto_cmake = block_proto_cmake(proto_basename)
    targets = (
        "add_executable(async_http_backend src/backend.cc)\n"
        "target_link_libraries(async_http_backend mprpc " + proto_basename + "_proto protobuf)\n"
        "\n"
        "add_executable(async_http_server src/server.cc)\n"
        "target_link_libraries(async_http_server mprpc " + proto_basename + "_proto protobuf)\n")
    write_cmake(proj_dir, name, proto_cmake, targets)

    write_shell_scripts(args.force, proj_dir, name,
                        ["async_http_backend", "async_http_server"],
                        {"async_http_backend": name + "_backend.conf",
                         "async_http_server": name + "_server.conf"})
    write_project_readme(proj_dir, name, "async_http",
                         readme_async_http(name, args.server_port))
    return proj_dir


# ------------------------------------------------------------------------ cli
def build_parser():
    parser = ArgumentParser(description="coroutine_rpc example generator",
                            add_help=True)
    parser.add_argument("mode", choices=["rpc", "http", "async_http"],
                        help="example type to generate")
    parser.add_argument("-i", "--proto", default=None,
                        help="input proto3 file with cc_generic_services")
    parser.add_argument("-o", "--output", default=".",
                        help="output parent dir (project goes to {output}/{name}/)")
    parser.add_argument("-n", "--name", default=None,
                        help="project name (default: proto basename or 'demo')")
    parser.add_argument("--demo", default="auto",
                        choices=["auto", "user", "friend", "queryage", "none"],
                        help="business profile selection")
    parser.add_argument("--zk-port", type=int, default=2181,
                        help="ZooKeeper port written into conf")
    parser.add_argument("--force", action="store_true",
                        help="overwrite existing business files")
    parser.add_argument("--provider-port", type=int, default=8000,
                        help="rpc: provider listen port")
    parser.add_argument("--servlet-path", action="append", default=None,
                        help="http: servlet URL path (repeatable)")
    parser.add_argument("--port", type=int, default=8080,
                        help="http: HTTP listen port")
    parser.add_argument("--server-port", type=int, default=8100,
                        help="async_http: HTTP frontend listen port")
    parser.add_argument("--backend-port", type=int, default=8101,
                        help="async_http: RPC backend listen port")
    return parser


def main():
    parser = build_parser()
    args = parser.parse_args()
    try:
        if args.mode == "rpc":
            gen_rpc(args)
        elif args.mode == "http":
            gen_http(args)
        else:
            gen_async_http(args)
        print("Succ generate " + args.mode + " example project.")
    except Exception as e:
        print("Failed generate project, err: " + str(e))
        sys.exit(1)


if __name__ == "__main__":
    main()
