#include <types.h>
#include <mips/trapframe.h>
#include <kern/errno.h>
#include <kern/unistd.h>
#include <kern/wait.h>
#include <lib.h>
#include <syscall.h>
#include <current.h>
#include <proc.h>
#include <thread.h>
#include <addrspace.h>
#include <copyinout.h>
#include <synch.h>
#include <vfs.h>
#include <kern/fcntl.h>
  /* this implementation of sys__exit does not do anything with the exit code */
  /* this needs to be fixed to get exit() and waitpid() working properly */

int sys_fork(struct trapframe * tf, pid_t *retval){
  struct proc *p = curproc; /* For debugging */
  (void)p;
  struct proc * child_proc = proc_create_runprogram("chd");
  if(child_proc == NULL){
     return ENOMEM;
  }
  struct addrspace * chd_as = as_create();
  if(chd_as == NULL){
      return ENOMEM;  
  }
  int copy_result = as_copy(curproc->p_addrspace, &chd_as);
  if(copy_result){
    return copy_result;
  }
  spinlock_acquire(&child_proc->p_lock);
  child_proc->p_addrspace = chd_as;
  spinlock_release(&child_proc->p_lock);
  child_proc->parent_pid = curproc->pid;
  struct trapframe * trp = kmalloc(sizeof(struct trapframe));
  memcpy( trp, tf, sizeof(struct trapframe));
  *retval = child_proc->pid;
  thread_fork("chd", child_proc, enter_forked_process, (void *)trp, (int)chd_as);
  return 0;
}

void sys__exit(int exitcode) {
  // kprintf("exit: %d\n", exitcode);
  struct addrspace *as;
  struct proc *p = curproc;

  /* for now, just include this to keep the compiler from complaining about
     an unused variable */
  global_process_array[curproc->pid - 2]->exit_code = _MKWAIT_EXIT(exitcode);
  global_process_array[curproc->pid - 2]->ex = 1;

  unsigned int x = array_num(curproc->children);
  int z;
  lock_acquire(glb_arr_lck);
  for(unsigned int i = 0; i < x; i++){
      z = (int)array_get(curproc->children, i) - 2;
      if(global_process_array[z]->ex == 1){
        global_process_array[z]->ex = 0;
        global_process_array[z]->proc_id = -1;
        global_process_array[z]->exists = 0;
      }
  }
  V(global_process_array[curproc->pid - 2]->sm);
  lock_release(glb_arr_lck);
  DEBUG(DB_SYSCALL,"Syscall: _exit(%d)\n",exitcode);

  KASSERT(curproc->p_addrspace != NULL);
  as_deactivate();
  /*
   * clear p_addrspace before calling as_destroy. Otherwise if
   * as_destroy sleeps (which is quite possible) when we
   * come back we'll be calling as_activate on a
   * half-destroyed address space. This tends to be
   * messily fatal.
   */
  as = curproc_setas(NULL);
  as_destroy(as);

  /* detach this thread from its process */
  /* note: curproc cannot be used after this call */
  proc_remthread(curthread);

  /* if this is the last user process in the system, proc_destroy()
     will wake up the kernel menu thread */
  proc_destroy(p);
  
  thread_exit();
  /* thread_exit() does not return, so we should never get here */
  panic("return from thread_exit in sys_exit\n");
}


/* stub handler for getpid() system call                */
int
sys_getpid(pid_t *retval)
{
  
  *retval = curproc->pid;
  return(0);
}

/* stub handler for waitpid() system call                */

int
sys_waitpid(pid_t pid,
	    userptr_t status,
	    int options,
	    pid_t *retval)
{
  int exitstatus;
  int result;

  if(global_process_array[pid-2]->exists != 1){
    return ESRCH;
  }
  else if(global_process_array[pid-2]->ex != 1){
        P(global_process_array[pid-2]->sm);
  }
  exitstatus = global_process_array[pid-2]->exit_code;
  //kprintf("exit: %d\n", exitstatus);
  global_process_array[pid-2]->exists = 0;
  global_process_array[pid-2]->proc_id = -1;
  if (options != 0) {
    return(EINVAL);
  }
  /* for now, just pretend the exitstatus is 0 */
  result = copyout((void *)&exitstatus,status,sizeof(int));
  if (result) {
    return(result);
  }

  *retval = pid;
  return(0);
}




int sys_execv(const char *program, char **args){
  int argc;
  for (argc = 0; args[argc] != NULL; argc++) {}
  struct addrspace *as;
  struct vnode *v;
  vaddr_t entrypoint, stackptr;
  int result;
  char *fname_temp;
  fname_temp = kstrdup(program);
  result = vfs_open(fname_temp, O_RDONLY, 0, &v);
  if(result){
      return result;
  }
  as = as_create();
  if(as == NULL){
      vfs_close(v);
      return ENOMEM;
  }
  int bbb = (argc+1)*sizeof(char *);
  int total_len = 0;
  char **args_temp = kmalloc(bbb);
  vaddr_t *args_temp_2 = kmalloc(bbb);
  args_temp_2[argc] = 0;
  args_temp[argc] = NULL;
  int len;
  for(int i = 0; i < argc; i++){
    len = strlen(args[i]) + 1;
    args_temp[i] = kmalloc(sizeof(char) * len);
    copyin((const_userptr_t)args[i], (void *) args_temp[i], len);
    total_len = total_len + ROUNDUP(len, 8);
  }
  as_deactivate();
  as_destroy(curproc->p_addrspace);
  curproc_setas(as);
  as_activate();
  result = load_elf(v, &entrypoint);
  if(result){
      vfs_close(v);
      return result;
  }
  
  vfs_close(v);
  kfree(fname_temp);
  result = as_define_stack(as, &stackptr);
  if(result){
      return result;
  }
  stackptr -= total_len;
  stackptr -= bbb;
  paddr_t p = stackptr + bbb;
  for(int i = 0; i < argc; i++){
    len = strlen(args_temp[i]) + 1;
    len = ROUNDUP(len, 8);
    args_temp_2[i] = p;
    copyout(args_temp[i], (userptr_t)p, len);
    p = p + len;
  }
  copyout(args_temp_2, (userptr_t)stackptr, bbb);
  for(int i = 0; i < argc; i++){
    kfree(args_temp[i]);
  }
  kfree(args_temp);
  kfree(args_temp_2);
  enter_new_process(argc, (userptr_t)stackptr, stackptr, entrypoint);

  return -1;
}
