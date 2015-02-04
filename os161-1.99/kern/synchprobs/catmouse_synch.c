#include <types.h>
#include <lib.h>
#include <synchprobs.h>
#include <synch.h>

/* 
 * This simple default synchronization mechanism allows only creature at a time to
 * eat.   The globalCatMouseSem is used as a a lock.   We use a semaphore
 * rather than a lock so that this code will work even before locks are implemented.
 */

/* 
 * Replace this default synchronization mechanism with your own (better) mechanism
 * needed for your solution.   Your mechanism may use any of the available synchronzation
 * primitives, e.g., semaphores, locks, condition variables.   You are also free to 
 * declare other global variables if your solution requires them.
 */

/*
 * replace this with declarations of any synchronization and other variables you need here
 */
static struct lock *globalCatMouseLock;
static struct cv *globalCatCV;
static struct cv *globalMouseCV;
static volatile char *bowls_ar;
static int num_cats_eating;
static int num_mice_eating;
static int numBowls;
static int cats_waiting;
static int mice_waiting;
static int cat_count;
static int mouse_count;



/* 
 * The CatMouse simulation will call this function once before any cat or
 * mouse tries to each.
 *
 * You can use it to initialize synchronization and other variables.
 * 
 * parameters: the number of bowls
 */
void
catmouse_sync_init(int bowls)
{
  (void)bowls;
  KASSERT(bowls > 0);
  bowls_ar = kmalloc(bowls * sizeof(char));
  if(bowls_ar == NULL){
     panic("initialize_bowls: unable to allocate space for %d bowls\n",numBowls); 
  }
  for(int i=0; i < bowls; i++){
      bowls_ar[i] = '-';
  }
  globalCatMouseLock = lock_create("globalCatMouseLock");
  if (globalCatMouseLock == NULL) {
    panic("could not create global CatMouse synchronization lock");
  }
  globalCatCV = cv_create("globalCatCV");
  if (globalCatCV == NULL) {
    panic("could not create global Cat cv");
  }
  globalMouseCV = cv_create("globalMouseCV");
  if (globalMouseCV == NULL) {
    panic("could not create global Mouse cv");
  }
  num_cats_eating = num_mice_eating = 0;
  cats_waiting = mice_waiting = 0;
  cat_count = mouse_count = 0;
  numBowls = bowls;
  return;
}

/* 
 * The CatMouse simulation will call this function once after all cat
 * and mouse simulations are finished.
 *
 * You can use it to clean up any synchronization and other variables.
 *
 * parameters: the number of bowls
 */
void
catmouse_sync_cleanup(int bowls)
{
  (void)bowls;
  KASSERT(globalCatMouseLock != NULL);
  kfree(globalCatMouseLock);
  KASSERT(globalCatCV != NULL);
  kfree(globalCatCV);
  KASSERT(globalMouseCV != NULL);
  kfree(globalMouseCV);
  KASSERT(bowls_ar != NULL);
  kfree( (void *) bowls_ar);
  bowls_ar = NULL;
}


/*
 * The CatMouse simulation will call this function each time a cat wants
 * to eat, before it eats.
 * This function should cause the calling thread (a cat simulation thread)
 * to block until it is OK for a cat to eat at the specified bowl.
 *
 * parameter: the number of the bowl at which the cat is trying to eat
 *             legal bowl numbers are 1..NumBowls
 *
 * return value: none
 */

void
cat_before_eating(unsigned int bowl) 
{ 
  KASSERT(globalCatMouseLock != NULL);
  KASSERT(globalCatCV != NULL);
  KASSERT(globalMouseCV != NULL);
  lock_acquire(globalCatMouseLock);
  cats_waiting++;
  while(bowls_ar[bowl-1] != '-' || cat_count == numBowls || num_mice_eating > 0){
    cv_wait(globalCatCV, globalCatMouseLock);
  }
  cats_waiting--;
  cat_count++;
  bowls_ar[bowl-1] = 'c';
  num_cats_eating++;
  lock_release(globalCatMouseLock);
}

/*
 * The CatMouse simulation will call this function each time a cat finishes
 * eating.
 *
 * You can use this function to wake up other creatures that may have been
 * waiting to eat until this cat finished.
 *
 * parameter: the number of the bowl at which the cat is finishing eating.
 *             legal bowl numbers are 1..NumBowls
 *
 * return value: none
 */

void
cat_after_eating(unsigned int bowl) 
{
  KASSERT(globalCatMouseLock != NULL);
  KASSERT(globalCatCV != NULL);
  KASSERT(globalMouseCV != NULL);
  lock_acquire(globalCatMouseLock);
  bowls_ar[bowl-1] = '-';
  num_cats_eating--;
  if(num_cats_eating == 0){
    cat_count = 0;
    if(mice_waiting > 0){
              cv_broadcast(globalMouseCV, globalCatMouseLock);
    }
    else{
        cv_broadcast(globalCatCV, globalCatMouseLock);
    }
  }
  lock_release(globalCatMouseLock);
}

/*
 * The CatMouse simulation will call this function each time a mouse wants
 * to eat, before it eats.
 * This function should cause the calling thread (a mouse simulation thread)
 * to block until it is OK for a mouse to eat at the specified bowl.
 *
 * parameter: the number of the bowl at which the mouse is trying to eat
 *             legal bowl numbers are 1..NumBowls
 *
 * return value: none
 */

void
mouse_before_eating(unsigned int bowl) 
{
  KASSERT(globalCatMouseLock != NULL);
  KASSERT(globalCatCV != NULL);
  KASSERT(globalMouseCV != NULL);
  lock_acquire(globalCatMouseLock);
  mice_waiting++;
  while(bowls_ar[bowl-1] != '-' || mouse_count == numBowls || num_cats_eating > 0){
    cv_wait(globalMouseCV, globalCatMouseLock);
  }
  mice_waiting--;
  mouse_count++;
  bowls_ar[bowl-1] = 'm';
  num_mice_eating++;
  lock_release(globalCatMouseLock);
}

/*
 * The CatMouse simulation will call this function each time a mouse finishes
 * eating.
 *
 * You can use this function to wake up other creatures that may have been
 * waiting to eat until this mouse finished.
 *
 * parameter: the number of the bowl at which the mouse is finishing eating.
 *             legal bowl numbers are 1..NumBowls
 *
 * return value: none
 */

void
mouse_after_eating(unsigned int bowl) 
{
  KASSERT(globalCatMouseLock != NULL);
  KASSERT(globalCatCV != NULL);
  KASSERT(globalMouseCV != NULL);
  lock_acquire(globalCatMouseLock);
  bowls_ar[bowl-1] = '-';
  num_mice_eating--;
  if(num_mice_eating == 0){  
    mouse_count = 0;    
    if(cats_waiting > 0){
      cv_broadcast(globalCatCV, globalCatMouseLock);
    }
    else{
        cv_broadcast(globalMouseCV, globalCatMouseLock);
    }
  }
  lock_release(globalCatMouseLock);
}
