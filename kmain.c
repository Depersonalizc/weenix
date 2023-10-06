#include "errno.h"
#include "globals.h"
#include "types.h"
#include <api/exec.h>
#include <drivers/screen.h>
#include <drivers/tty/tty.h>
#include <drivers/tty/vterminal.h>
#include <main/io.h>
#include <mm/mm.h>
#include <mm/slab.h>
#include <test/kshell/kshell.h>
#include <util/time.h>
#include <vm/anon.h>
#include <vm/shadow.h>

#include "util/debug.h"
#include "util/gdb.h"
#include "util/printf.h"
#include "util/string.h"

#include "main/acpi.h"
#include "main/apic.h"
#include "main/inits.h"

#include "drivers/dev.h"
#include "drivers/pcie.h"

#include "api/syscall.h"

#include "fs/fcntl.h"
#include "fs/vfs.h"
#include "fs/vfs_syscall.h"
#include "fs/vnode.h"

#include "test/driverstest.h"
#include "test/proctest.h"
#include "test/vmtest.h"
#include "test/kshell/kshell.h"

#define DO_KTEST 0

GDB_DEFINE_HOOK(boot)

GDB_DEFINE_HOOK(initialized)

GDB_DEFINE_HOOK(shutdown)

static void initproc_start();

typedef void (*init_func_t)();
static init_func_t init_funcs[] = {
    dbg_init,
    intr_init,
    page_init,
    pt_init,
    acpi_init,
    apic_init,
    core_init,
    slab_init,
    pframe_init,
    pci_init,
    vga_init,
#ifdef __VM__
    anon_init,
    shadow_init,
#endif
    vmmap_init,
    proc_init,
    kthread_init,
#ifdef __DRIVERS__
    chardev_init,
    blockdev_init,
#endif
    kshell_init,
    file_init,
    pipe_init,
    syscall_init,
    elf64_init,

#ifdef __SMP__
    smp_init,
#endif

    proc_idleproc_init,
};

/*
 * Call the init functions (in order!), then run the init process
 * (initproc_start)
 */
void kmain()
{
    GDB_CALL_HOOK(boot);

    for (size_t i = 0; i < sizeof(init_funcs) / sizeof(init_funcs[0]); i++)
        init_funcs[i]();

    initproc_start();
    panic("\nReturned to kmain()\n");
}

/*
 * Make:
 * 1) /dev/null
 * 2) /dev/zero
 * 3) /dev/ttyX for 0 <= X < __NTERMS__
 * 4) /dev/hdaX for 0 <= X < __NDISKS__
 */
static void make_devices()
{
    dbg(DBG_VFS, "Entering make_devices\n");
    long status = do_mkdir("/dev");
    KASSERT(!status || status == -EEXIST);

    status = do_mknod("/dev/null", S_IFCHR, MEM_NULL_DEVID);
    KASSERT(!status || status == -EEXIST);
    status = do_mknod("/dev/zero", S_IFCHR, MEM_ZERO_DEVID);
    KASSERT(!status || status == -EEXIST);

    char path[32] = {0};
    for (long i = 0; i < __NTERMS__; i++)
    {
        snprintf(path, sizeof(path), "/dev/tty%ld", i);
        dbg(DBG_INIT, "Creating tty mknod with path %s\n", path);
        status = do_mknod(path, S_IFCHR, MKDEVID(TTY_MAJOR, i));
        KASSERT(!status || status == -EEXIST);
    }

    for (long i = 0; i < __NDISKS__; i++)
    {
        snprintf(path, sizeof(path), "/dev/hda%ld", i);
        dbg(DBG_INIT, "Creating disk mknod with path %s\n", path);
        status = do_mknod(path, S_IFBLK, MKDEVID(DISK_MAJOR, i));
        KASSERT(!status || status == -EEXIST);
    }
}


/*
 * The function executed by the init process. Finish up all initialization now 
 * that we have a proper thread context.
 * 
 * This function will require edits over the course of the project:
 *
 * - Before finishing drivers, this is where your tests lie. You can, however, 
 *  have them in a separate test function which can even be in a separate file 
 *  (see handout).
 * 
 * - After finishing drivers but before starting VM, you should start __NTERMS__
 *  processes running kshells (see kernel/test/kshell/kshell.c, specifically
 *  kshell_proc_run). Testing here amounts to defining a new kshell command 
 *  that runs your tests. 
 * 
 * - During and after VM, you should use kernel_execve when starting, you
 *  will probably want to kernel_execve the program you wish to test directly.
 *  Eventually, you will want to kernel_execve "/sbin/init" and run your
 *  tests from the userland shell (by typing in test commands)
 * 
 * Note: The init process should wait on all of its children to finish before 
 * returning from this function (at which point the system will shut down).
 */
static void *initproc_run(long arg1, void *arg2)
{
    dbg(DBG_INIT, "initproc enters initproc_run\n");

    KASSERT(curproc->p_pid == PID_INIT);

    /* Testing Procs */
#if DO_KTEST
    proctest_main(0, NULL);
#endif

#ifdef __VFS__
    dbg(DBG_INIT, "Initializing VFS...\n");
    vfs_init();
    make_devices();
#endif

#ifdef __DRIVERS__
#if DO_KTEST
    driverstest_main(0, NULL);
#endif
#endif

#ifdef __VM__
#if DO_KTEST
    vmtest_main(0, NULL);
#endif
    char *argv[1] = {NULL};
    char *envp[1] = {NULL};
    kernel_execve("/sbin/init", argv, envp);  // need fork to work
#endif

    dbg(DBG_INIT, "initproc returns from initproc_run\n");
    return NULL;
}

/*
 * Sets up the initial process and prepares it to run.
 *
 * Hints:
 * Use proc_create() to create the initial process.
 * Use kthread_create() to create the initial process's only thread.
 * Make sure the thread is set up to start running initproc_run() (values for
 *  arg1 and arg2 do not matter, they can be 0 and NULL).
 * Use sched_make_runnable() to make the thread runnable.
 * Use context_make_active() with the context of the current core (curcore) 
 * to start the scheduler.
 */
void initproc_start()
{
//    N.OT_YET_IMPLEMENTED("PROCS: initproc_start");
    dbg(DBG_ALL, "Weenix init proc starts\n");

    proc_t *init_proc;
    kthread_t *init_thr;

    // Create init process
    if ( (init_proc = proc_create("init\0")) == NULL )
        panic("failed to create init process\n");
    KASSERT(init_proc->p_pid == PID_INIT);

    // Create single thread for the init process which executes initproc_run
    if ( (init_thr = kthread_create(
            init_proc, initproc_run, 0, NULL)) == NULL )
        panic("failed to create init");

    dbg(DBG_ALL, "Scheduling init thread\n");
    sched_make_runnable(init_thr);        // Put init thread on the runq

    dbg(DBG_ALL, "Firing up scheduler\n");
    context_make_active(&curcore.kc_ctx);  // Fire the scheduler on this core

    dbg(DBG_ALL, "Leaving initproc_start\n");
}

void initproc_finish()
{
    dbg(DBG_ALL, "Weenix shutdown!\n");
//    panic("HOLDUP");

#ifdef __VFS__
    if (vfs_shutdown())
        panic("vfs shutdown FAILED!!\n");
#endif

#ifdef __DRIVERS__
    screen_print_shutdown();
#endif

    /* HALT */
    while (1)
    {
        __asm__ volatile("cli; hlt;");
    }

    panic("should not get here");
}
