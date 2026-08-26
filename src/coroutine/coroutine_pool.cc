#include <vector>
#include "comm/config.h"
#include "comm/log.h"
#include "coroutine/coroutine_pool.h"
#include "coroutine/coroutine.h"
#include "net/mutex.h"

namespace crpc {

static CoroutinePool* t_coroutine_container_ptr = nullptr;

CoroutinePool* GetCoroutinePool() {
  if (!t_coroutine_container_ptr) {
    t_coroutine_container_ptr = new CoroutinePool(GetConfig()->m_cor_pool_size, GetConfig()->m_cor_stack_size);
  }
  return t_coroutine_container_ptr;
}

CoroutinePool::CoroutinePool(int pool_size, int stack_size /*= 1024 * 128 B*/)
    : m_pool_size(pool_size), m_stack_size(stack_size) {
  // set main coroutine first
  Coroutine::GetCurrentCoroutine();

  m_memory_pool.push_back(std::make_shared<Memory>(stack_size, pool_size));

  Memory::ptr tmp = m_memory_pool[0];

  for (int i = 0; i < pool_size; ++i) {
    Coroutine::ptr cor = std::make_shared<Coroutine>(stack_size, tmp->getBlock());
    cor->setIndex(i);
    m_free_cors.push_back(std::make_pair(cor, false));
  }
}

CoroutinePool::~CoroutinePool() {}

Coroutine::ptr CoroutinePool::getCoroutineInstanse() {
  // try best to reuse used coroutine, prefer not to choose unused one to avoid page fault
  Mutex::Lock lock(m_mutex);
  for (int i = 0; i < m_pool_size; ++i) {
    if (!m_free_cors[i].first->getIsInCoFunc() && !m_free_cors[i].second) {
      m_free_cors[i].second = true;
      Coroutine::ptr cor = m_free_cors[i].first;
      lock.unlock();
      return cor;
    }
  }

  for (size_t i = 1; i < m_memory_pool.size(); ++i) {
    char* tmp = m_memory_pool[i]->getBlock();
    if (tmp) {
      Coroutine::ptr cor = std::make_shared<Coroutine>(m_stack_size, tmp);
      return cor;
    }
  }
  m_memory_pool.push_back(std::make_shared<Memory>(m_stack_size, m_pool_size));
  return std::make_shared<Coroutine>(m_stack_size, m_memory_pool[m_memory_pool.size() - 1]->getBlock());
}

void CoroutinePool::returnCoroutine(Coroutine::ptr cor) {
  int i = cor->getIndex();
  if (i >= 0 && i < m_pool_size) {
    m_free_cors[i].second = false;
  } else {
    for (size_t j = 1; j < m_memory_pool.size(); ++j) {
      if (m_memory_pool[j]->hasBlock(cor->getStackPtr())) {
        m_memory_pool[j]->backBlock(cor->getStackPtr());
      }
    }
  }
}

}  // namespace crpc
