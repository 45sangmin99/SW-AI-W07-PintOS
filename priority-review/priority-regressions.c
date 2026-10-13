/* Additional behavioral tests, installed only into disposable test copies. */
#include <debug.h>
#include <stdbool.h>
#include "devices/timer.h"
#include "tests/threads/tests.h"
#include "threads/interrupt.h"
#include "threads/synch.h"
#include "threads/thread.h"

void (*priority_regression_tick_hook)(void);

void test_regression_interrupt_sema(void);
void test_regression_ready_donation(void);
void test_regression_base_raise(void);
void test_regression_same_lock_cleanup(void);
void test_regression_cond_live(void);
void test_regression_cond_before_down(void);
void test_regression_alarm_preempt(void);
void test_regression_lock_handoff(void);
void test_regression_lock_retry(void);

static struct semaphore interrupt_gate, interrupt_done;
static void interrupt_waiter(void *aux UNUSED)
{
  sema_down(&interrupt_gate);
  ASSERT(!intr_context());
  sema_up(&interrupt_done);
}
static void interrupt_up(void)
{
  ASSERT(intr_context());
  priority_regression_tick_hook = NULL;
  sema_up(&interrupt_gate);
}
void test_regression_interrupt_sema(void)
{
  thread_set_priority(20);
  sema_init(&interrupt_gate, 0);
  sema_init(&interrupt_done, 0);
  thread_create("irq-waiter", 50, interrupt_waiter, NULL);
  priority_regression_tick_hook = interrupt_up;
  sema_down(&interrupt_done);
  pass();
}

static struct lock ready_lock;
static struct semaphore ready_started, ready_done;
static bool holder_ran, medium_ran;
static void ready_holder(void *aux UNUSED)
{
  lock_acquire(&ready_lock);
  sema_up(&ready_started);
  ASSERT(!medium_ran);
  holder_ran = true;
  lock_release(&ready_lock);
  sema_up(&ready_done);
}
static void ready_medium(void *aux UNUSED)
{
  medium_ran = true;
  sema_up(&ready_done);
}
static void ready_donor(void *aux UNUSED)
{
  lock_acquire(&ready_lock);
  lock_release(&ready_lock);
  sema_up(&ready_done);
}
void test_regression_ready_donation(void)
{
  thread_set_priority(30);
  lock_init(&ready_lock);
  sema_init(&ready_started, 0);
  sema_init(&ready_done, 0);
  holder_ran = medium_ran = false;
  enum intr_level old = intr_disable();
  thread_create("ready-holder", 5, ready_holder, NULL);
  sema_down(&ready_started);
  thread_create("ready-medium", 20, ready_medium, NULL);
  thread_create("ready-donor", 50, ready_donor, NULL);
  ASSERT(holder_ran);
  intr_set_level(old);
  for (int i = 0; i < 3; i++) sema_down(&ready_done);
  pass();
}

static struct lock base_lock;
static struct semaphore base_done;
static void base_donor(void *aux UNUSED)
{
  lock_acquire(&base_lock);
  lock_release(&base_lock);
  sema_up(&base_done);
}
void test_regression_base_raise(void)
{
  thread_set_priority(10);
  lock_init(&base_lock);
  sema_init(&base_done, 0);
  lock_acquire(&base_lock);
  thread_create("base-donor", 40, base_donor, NULL);
  ASSERT(thread_get_priority() == 40);
  thread_set_priority(50);
  ASSERT(thread_get_priority() == 50);
  lock_release(&base_lock);
  ASSERT(thread_get_priority() == 50);
  sema_down(&base_done);
  pass();
}
void test_regression_same_lock_cleanup(void)
{
  thread_set_priority(10);
  lock_init(&base_lock);
  sema_init(&base_done, 0);
  lock_acquire(&base_lock);
  thread_create("same-donor-40", 40, base_donor, NULL);
  thread_create("same-donor-50", 50, base_donor, NULL);
  ASSERT(thread_get_priority() == 50);
  lock_release(&base_lock);
  sema_down(&base_done);
  sema_down(&base_done);
  ASSERT(thread_get_priority() == 10);
  pass();
}

static struct lock live_monitor, live_donation;
static struct condition live_condition;
static struct semaphore live_done;
static int live_first;
static void live_low(void *aux UNUSED)
{
  lock_acquire(&live_donation);
  lock_acquire(&live_monitor);
  cond_wait(&live_condition, &live_monitor);
  if (live_first == -1) live_first = 0;
  lock_release(&live_monitor);
  lock_release(&live_donation);
  sema_up(&live_done);
}
static void live_high(void *aux UNUSED)
{
  lock_acquire(&live_monitor);
  cond_wait(&live_condition, &live_monitor);
  if (live_first == -1) live_first = 1;
  lock_release(&live_monitor);
  sema_up(&live_done);
}
static void live_donor(void *aux UNUSED)
{
  lock_acquire(&live_donation);
  lock_release(&live_donation);
  sema_up(&live_done);
}
void test_regression_cond_live(void)
{
  thread_set_priority(20);
  lock_init(&live_monitor);
  lock_init(&live_donation);
  cond_init(&live_condition);
  sema_init(&live_done, 0);
  live_first = -1;
  thread_create("condition-low", 30, live_low, NULL);
  thread_create("condition-high", 40, live_high, NULL);
  thread_create("condition-donor", 50, live_donor, NULL);
  lock_acquire(&live_monitor);
  cond_signal(&live_condition, &live_monitor);
  lock_release(&live_monitor);
  ASSERT(live_first == 0);
  lock_acquire(&live_monitor);
  cond_broadcast(&live_condition, &live_monitor);
  lock_release(&live_monitor);
  for (int i = 0; i < 3; i++) sema_down(&live_done);
  pass();
}

static struct lock early_monitor;
static struct condition early_condition;
static struct semaphore early_signal, early_done;
static int early_first_awake;
static void early_first(void *aux UNUSED)
{
  lock_acquire(&early_monitor);
  cond_wait(&early_condition, &early_monitor);
  if (early_first_awake == -1) early_first_awake = 0;
  lock_release(&early_monitor);
  sema_up(&early_done);
}
static void early_second(void *aux UNUSED)
{
  lock_acquire(&early_monitor);
  sema_up(&early_signal);
  /* Signaler now waits for this lock, donating its priority. */
  cond_wait(&early_condition, &early_monitor);
  if (early_first_awake == -1) early_first_awake = 1;
  lock_release(&early_monitor);
  sema_up(&early_done);
}
static void early_signaler(void *aux UNUSED)
{
  sema_down(&early_signal);
  lock_acquire(&early_monitor);
  /* The second waiter has released the lock but has not downed its sema. */
  cond_signal(&early_condition, &early_monitor);
  lock_release(&early_monitor);
  sema_up(&early_done);
}
void test_regression_cond_before_down(void)
{
  thread_set_priority(20);
  lock_init(&early_monitor);
  cond_init(&early_condition);
  sema_init(&early_signal, 0);
  sema_init(&early_done, 0);
  early_first_awake = -1;
  thread_create("early-signaler", 50, early_signaler, NULL);
  thread_create("early-first", 30, early_first, NULL);
  thread_create("early-second", 40, early_second, NULL);
  sema_down(&early_done);
  ASSERT(early_first_awake == 1);
  lock_acquire(&early_monitor);
  cond_broadcast(&early_condition, &early_monitor);
  lock_release(&early_monitor);
  for (int i = 0; i < 2; i++) sema_down(&early_done);
  pass();
}

static volatile int64_t alarm_deadline;
static volatile bool alarm_ran;
static void alarm_waiter(void *aux UNUSED)
{
  alarm_deadline = timer_ticks() + 1;
  timer_sleep(1);
  alarm_ran = true;
}
void test_regression_alarm_preempt(void)
{
  thread_set_priority(20);
  alarm_ran = false;
  /* Begin a new time slice just after a tick, so the alarm precedes expiry. */
  int64_t start = timer_ticks();
  while (timer_ticks() == start) barrier();
  thread_yield();
  thread_create("alarm-waiter", 40, alarm_waiter, NULL);
  while (timer_ticks() < alarm_deadline) barrier();
  ASSERT(alarm_ran);
  pass();
}

static struct lock handoff_lock;
static struct semaphore handoff_done;
static void handoff_first(void *aux UNUSED)
{
  lock_acquire(&handoff_lock);
  thread_set_priority(5);
  /* The other waiter still needs this lock and must donate to its new owner. */
  ASSERT(thread_get_priority() == 40);
  lock_release(&handoff_lock);
  ASSERT(thread_get_priority() == 5);
  sema_up(&handoff_done);
}
static void handoff_second(void *aux UNUSED)
{
  lock_acquire(&handoff_lock);
  lock_release(&handoff_lock);
  sema_up(&handoff_done);
}
void test_regression_lock_handoff(void)
{
  thread_set_priority(10);
  lock_init(&handoff_lock);
  sema_init(&handoff_done, 0);
  lock_acquire(&handoff_lock);
  thread_create("handoff-second", 40, handoff_second, NULL);
  thread_create("handoff-first", 50, handoff_first, NULL);
  lock_release(&handoff_lock);
  sema_down(&handoff_done);
  sema_down(&handoff_done);
  ASSERT(thread_get_priority() == 10);
  pass();
}

static struct lock retry_lock;
static struct semaphore retry_started, retry_release, retry_medium_gate, retry_done;
static bool retry_medium_ran;
static void retry_owner(void *aux UNUSED)
{
  lock_acquire(&retry_lock);
  sema_up(&retry_started);
  sema_down(&retry_release);
  lock_release(&retry_lock);
  /* A higher-priority owner can reacquire before the awakened waiter runs. */
  ASSERT(lock_try_acquire(&retry_lock));
  sema_up(&retry_medium_gate);
  thread_set_priority(5);
  /* The waiter must retry its donation, not merely block again. */
  ASSERT(thread_get_priority() == 40);
  ASSERT(!retry_medium_ran);
  lock_release(&retry_lock);
  sema_up(&retry_done);
}
static void retry_waiter(void *aux UNUSED)
{
  lock_acquire(&retry_lock);
  lock_release(&retry_lock);
  sema_up(&retry_done);
}
static void retry_medium(void *aux UNUSED)
{
  sema_down(&retry_medium_gate);
  retry_medium_ran = true;
  sema_up(&retry_done);
}
void test_regression_lock_retry(void)
{
  thread_set_priority(20);
  lock_init(&retry_lock);
  sema_init(&retry_started, 0);
  sema_init(&retry_release, 0);
  sema_init(&retry_medium_gate, 0);
  sema_init(&retry_done, 0);
  retry_medium_ran = false;
  thread_create("retry-owner", 60, retry_owner, NULL);
  sema_down(&retry_started);
  thread_create("retry-waiter", 40, retry_waiter, NULL);
  thread_create("retry-medium", 30, retry_medium, NULL);
  sema_up(&retry_release);
  for (int i = 0; i < 3; i++) sema_down(&retry_done);
  pass();
}
