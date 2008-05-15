#include <sys/condvar.h>

#ifdef DEBUG_SUBSYSTEM
#undef DEBUG_SUBSYSTEM
#endif

#define DEBUG_SUBSYSTEM S_CONDVAR

void
__cv_init(kcondvar_t *cvp, char *name, kcv_type_t type, void *arg)
{
	int flags = KM_SLEEP;

	ENTRY;
	ASSERT(cvp);
	ASSERT(name);
	ASSERT(type == CV_DEFAULT);
	ASSERT(arg == NULL);

	cvp->cv_magic = CV_MAGIC;
	init_waitqueue_head(&cvp->cv_event);
	spin_lock_init(&cvp->cv_lock);
	atomic_set(&cvp->cv_waiters, 0);
	cvp->cv_mutex = NULL;
	cvp->cv_name = NULL;
	cvp->cv_name_size = strlen(name) + 1;

        /* We may be called when there is a non-zero preempt_count or
	 * interrupts are disabled is which case we must not sleep.
	 */
        if (current_thread_info()->preempt_count || irqs_disabled())
		flags = KM_NOSLEEP;

	cvp->cv_name = kmem_alloc(cvp->cv_name_size, flags);
	if (cvp->cv_name)
	        strcpy(cvp->cv_name, name);

	EXIT;
}
EXPORT_SYMBOL(__cv_init);

void
__cv_destroy(kcondvar_t *cvp)
{
	ENTRY;
	ASSERT(cvp);
	ASSERT(cvp->cv_magic == CV_MAGIC);
	spin_lock(&cvp->cv_lock);
	ASSERT(atomic_read(&cvp->cv_waiters) == 0);
	ASSERT(!waitqueue_active(&cvp->cv_event));

	if (cvp->cv_name)
		kmem_free(cvp->cv_name, cvp->cv_name_size);

	memset(cvp, CV_POISON, sizeof(*cvp));
	spin_unlock(&cvp->cv_lock);
	EXIT;
}
EXPORT_SYMBOL(__cv_destroy);

void
__cv_wait(kcondvar_t *cvp, kmutex_t *mp)
{
	DEFINE_WAIT(wait);
	ENTRY;

	ASSERT(cvp);
        ASSERT(mp);
	ASSERT(cvp->cv_magic == CV_MAGIC);
	spin_lock(&cvp->cv_lock);
	ASSERT(mutex_owned(mp));

	if (cvp->cv_mutex == NULL)
		cvp->cv_mutex = mp;

	/* Ensure the same mutex is used by all callers */
	ASSERT(cvp->cv_mutex == mp);
	spin_unlock(&cvp->cv_lock);

	prepare_to_wait_exclusive(&cvp->cv_event, &wait,
				  TASK_UNINTERRUPTIBLE);
	atomic_inc(&cvp->cv_waiters);

	/* Mutex should be dropped after prepare_to_wait() this
	 * ensures we're linked in to the waiters list and avoids the
	 * race where 'cvp->cv_waiters > 0' but the list is empty. */
	mutex_exit(mp);
	schedule();
	mutex_enter(mp);

	atomic_dec(&cvp->cv_waiters);
	finish_wait(&cvp->cv_event, &wait);
	EXIT;
}
EXPORT_SYMBOL(__cv_wait);

/* 'expire_time' argument is an absolute wall clock time in jiffies.
 * Return value is time left (expire_time - now) or -1 if timeout occurred.
 */
clock_t
__cv_timedwait(kcondvar_t *cvp, kmutex_t *mp, clock_t expire_time)
{
	DEFINE_WAIT(wait);
	clock_t time_left;
	ENTRY;

	ASSERT(cvp);
        ASSERT(mp);
	ASSERT(cvp->cv_magic == CV_MAGIC);
	spin_lock(&cvp->cv_lock);
	ASSERT(mutex_owned(mp));

	if (cvp->cv_mutex == NULL)
		cvp->cv_mutex = mp;

	/* Ensure the same mutex is used by all callers */
	ASSERT(cvp->cv_mutex == mp);
	spin_unlock(&cvp->cv_lock);

	/* XXX - Does not handle jiffie wrap properly */
	time_left = expire_time - jiffies;
	if (time_left <= 0)
		RETURN(-1);

	prepare_to_wait_exclusive(&cvp->cv_event, &wait,
				  TASK_UNINTERRUPTIBLE);
	atomic_inc(&cvp->cv_waiters);

	/* Mutex should be dropped after prepare_to_wait() this
	 * ensures we're linked in to the waiters list and avoids the
	 * race where 'cvp->cv_waiters > 0' but the list is empty. */
	mutex_exit(mp);
	time_left = schedule_timeout(time_left);
	mutex_enter(mp);

	atomic_dec(&cvp->cv_waiters);
	finish_wait(&cvp->cv_event, &wait);

	RETURN(time_left > 0 ? time_left : -1);
}
EXPORT_SYMBOL(__cv_timedwait);

void
__cv_signal(kcondvar_t *cvp)
{
	ENTRY;
	ASSERT(cvp);
	ASSERT(cvp->cv_magic == CV_MAGIC);

	/* All waiters are added with WQ_FLAG_EXCLUSIVE so only one
	 * waiter will be set runable with each call to wake_up().
	 * Additionally wake_up() holds a spin_lock assoicated with
	 * the wait queue to ensure we don't race waking up processes. */
	if (atomic_read(&cvp->cv_waiters) > 0)
		wake_up(&cvp->cv_event);

	EXIT;
}
EXPORT_SYMBOL(__cv_signal);

void
__cv_broadcast(kcondvar_t *cvp)
{
	ASSERT(cvp);
	ASSERT(cvp->cv_magic == CV_MAGIC);
	ENTRY;

	/* Wake_up_all() will wake up all waiters even those which
	 * have the WQ_FLAG_EXCLUSIVE flag set. */
	if (atomic_read(&cvp->cv_waiters) > 0)
		wake_up_all(&cvp->cv_event);

	EXIT;
}
EXPORT_SYMBOL(__cv_broadcast);
