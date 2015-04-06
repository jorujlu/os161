/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <spinlock.h>
#include <proc.h>
#include <current.h>
#include <mips/tlb.h>
#include <addrspace.h>
#include <vm.h>
#include <thread.h>
#include <mips/trapframe.h>

/*
 * Dumb MIPS-only "VM system" that is intended to only be just barely
 * enough to struggle off the ground. You should replace all of this
 * code while doing the VM assignment. In fact, starting in that
 * assignment, this file is not included in your kernel!
 */

/* under dumbvm, always have 48k of user stack */
#define DUMBVM_STACKPAGES    12

/*
 * Wrap rma_stealmem in a spinlock.
 */
static struct spinlock stealmem_lock = SPINLOCK_INITIALIZER;
static bool boot_done = false;
static int free_frame_pos = 0;
static int num_frames = 0;
struct c_map{
		paddr_t physical_addr;
		int avail;
		int contiguos_num;
};

struct c_map * cm_array;

void
vm_bootstrap(void)
{
	spinlock_acquire(&stealmem_lock);
		paddr_t lo;
		paddr_t hi;
		ram_getsize(&lo, &hi);
		lo = ROUNDUP(lo, PAGE_SIZE);
		num_frames = (hi-lo)/PAGE_SIZE;
		cm_array = (struct c_map *)PADDR_TO_KVADDR(lo);
		paddr_t free_addr = lo + sizeof(struct c_map) * num_frames;
		free_addr = ROUNDUP(free_addr, PAGE_SIZE);
		free_frame_pos = (free_addr - lo) / PAGE_SIZE;
		for(int i = 0; i < num_frames; i++){
				cm_array[i].contiguos_num = 0;
				cm_array[i].avail = !(i < free_frame_pos);
				cm_array[i].physical_addr = lo + i * PAGE_SIZE;
		}
		boot_done = true;
	spinlock_release(&stealmem_lock);	
}

static
paddr_t
getppages(unsigned long npages)
{
	spinlock_acquire(&stealmem_lock);
	paddr_t addr = 0;
	if(boot_done){
		int i = free_frame_pos;
		while(i < num_frames){
				int k = npages;
				for(unsigned j = 0; j < npages; j++){
					if(cm_array[i+j].avail){
							k--;
					}
					else{
							i += j + 1;
							break;
					}
				}
				if(k == 0){
					addr = cm_array[i].physical_addr;
					for(unsigned m = 0; m < npages; m++){
							cm_array[i+m].avail = 0;
					}
					cm_array[i].contiguos_num = npages;
					break;
				}
		}
	}
	else{
		addr = ram_stealmem(npages);
	}	
	spinlock_release(&stealmem_lock);
	return addr;
}

/* Allocate/free some kernel-space virtual pages */
vaddr_t 
alloc_kpages(int npages)
{
	paddr_t pa;
	pa = getppages(npages);
	if (pa==0) {
		return 0;
	}
	return PADDR_TO_KVADDR(pa);
}

void 
free_kpages(vaddr_t addr)
{

	spinlock_acquire(&stealmem_lock);
		for(int i = free_frame_pos; i < num_frames; i++){
			paddr_t entry_addr = cm_array[i].physical_addr;
			int contiguos = cm_array[i].contiguos_num;
			if(entry_addr == (addr - MIPS_KSEG0)){
					if(contiguos > 0){
							cm_array[i].contiguos_num = 0;
							for(int j = 0; j < contiguos; j++){
								cm_array[i+j].avail = 1;
							}
							break;
					}
			}
		}
	spinlock_release(&stealmem_lock);
}

void
vm_tlbshootdown_all(void)
{
	panic("dumbvm tried to do tlb shootdown?!\n");
}

void
vm_tlbshootdown(const struct tlbshootdown *ts)
{
	(void)ts;
	panic("dumbvm tried to do tlb shootdown?!\n");
}

int
vm_fault(int faulttype, vaddr_t faultaddress)
{
	vaddr_t vbase1, vtop1, vbase2, vtop2, stackbase, stacktop;
	paddr_t paddr;
	int i;
	uint32_t ehi, elo;
	struct addrspace *as;
	int spl;
	faultaddress &= PAGE_FRAME;
	//kprintf("%d\n", PAGE_FRAME);
	DEBUG(DB_VM, "dumbvm: fault: 0x%x\n", faultaddress);

	switch (faulttype) {
	    case VM_FAULT_READONLY:
			kill_curthread(0, 1, 0);
	    case VM_FAULT_READ:
	    case VM_FAULT_WRITE:
		break;
	    default:
		return EINVAL;
	}

	if (curproc == NULL) {
		/*
		 * No process. This is probably a kernel fault early
		 * in boot. Return EFAULT so as to panic instead of
		 * getting into an infinite faulting loop.
		 */
		return EFAULT;
	}

	as = curproc_getas();
	if (as == NULL) {
		/*
		 * No address space set up. This is probably also a
		 * kernel fault early in boot.
		 */
		return EFAULT;
	}

	/* Assert that the address space has been set up properly. */



	KASSERT(as->as_vbase1 != 0);
	//KASSERT(as->as_pbase1 != 0);
	KASSERT(as->as_npages1 != 0);
	KASSERT(as->as_vbase2 != 0);
	//KASSERT(as->as_pbase2 != 0);
	KASSERT(as->as_npages2 != 0);
	//KASSERT(as->as_stackpbase != 0);
	KASSERT((as->as_vbase1 & PAGE_FRAME) == as->as_vbase1);
	//KASSERT((as->as_pbase1 & PAGE_FRAME) == as->as_pbase1);
	KASSERT((as->as_vbase2 & PAGE_FRAME) == as->as_vbase2);
	//KASSERT((as->as_pbase2 & PAGE_FRAME) == as->as_pbase2);
	//KASSERT((as->as_stackpbase & PAGE_FRAME) == as->as_stackpbase);

	vbase1 = as->as_vbase1;
	vtop1 = vbase1 + as->as_npages1 * PAGE_SIZE;
	vbase2 = as->as_vbase2;
	vtop2 = vbase2 + as->as_npages2 * PAGE_SIZE;
	stackbase = USERSTACK - DUMBVM_STACKPAGES * PAGE_SIZE;
	stacktop = USERSTACK;
	/*kprintf("this is vbase1: %d\n", vbase1);
	kprintf("this is vbase2: %d\n", vbase2);
	kprintf("this is vtop1: %d\n", vtop1);
	kprintf("this is vtop2: %d\n", vtop2);*/
	//kprintf("this is stackbase: %d\n", stackbase);
	//kprintf("this is stacktop: %x\n", stacktop);
	//kprintf("this is faultaddress: %x\n", faultaddress);

	if (faultaddress >= vbase1 && faultaddress < vtop1) {
		paddr = as->text_table[(int)((faultaddress - vbase1) / PAGE_SIZE)].phys_addr;
	}
	else if (faultaddress >= vbase2 && faultaddress < vtop2) {
		paddr = as->data_table[(int)((faultaddress - vbase2) / PAGE_SIZE)].phys_addr;
	}
	else if (faultaddress >= stackbase && faultaddress < stacktop) {
		paddr = as->stack_table[(int)((faultaddress - stackbase) / PAGE_SIZE)].phys_addr;
	}
	else {
		//kprintf("this is faultaddress: %x\n", faultaddress);
		return EFAULT;
	}
	//kprintf("this is faultaddress: %d\n", faultaddress);
	/* make sure it's page-aligned */
	KASSERT((paddr & PAGE_FRAME) == paddr);

	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();

	for (i=0; i<NUM_TLB; i++) {
		tlb_read(&ehi, &elo, i);
		if (elo & TLBLO_VALID) {
			continue;
		}
		ehi = faultaddress;
		elo = paddr | TLBLO_DIRTY | TLBLO_VALID;
		if(ehi >= vbase1 && ehi <= vtop1){
				if(as->elf_done){
					elo &= ~TLBLO_DIRTY;
				}
		}
		DEBUG(DB_VM, "dumbvm: 0x%x -> 0x%x\n", faultaddress, paddr);
		tlb_write(ehi, elo, i);
		splx(spl);
		return 0;
	}
	ehi = faultaddress;
	elo = paddr | TLBLO_DIRTY | TLBLO_VALID;
	if(ehi >= vbase1 && ehi <= vtop1){
				if(as->elf_done){
					elo &= ~TLBLO_DIRTY;
				}
	}
	tlb_random(ehi, elo);
	splx(spl);
	return 0;
}

struct addrspace *
as_create(void)
{
	struct addrspace *as = kmalloc(sizeof(struct addrspace));
	if (as==NULL) {
		return NULL;
	}

	as->as_vbase1 = 0;
	//as->as_pbase1 = 0;
	as->as_npages1 = 0;
	as->as_vbase2 = 0;
	//as->as_pbase2 = 0;
	as->as_npages2 = 0;
	//as->as_stackpbase = 0;

	return as;
}

void
as_destroy(struct addrspace *as)
{
	for(unsigned i = 0; i < as->as_npages1; i++){
			free_kpages(PADDR_TO_KVADDR(as->text_table[i].phys_addr));
	}
	for(unsigned i = 0; i < as->as_npages2; i++){
			free_kpages(PADDR_TO_KVADDR(as->data_table[i].phys_addr));
	}
	for(unsigned i = 0; i < DUMBVM_STACKPAGES; i++){
			free_kpages(PADDR_TO_KVADDR(as->stack_table[i].phys_addr));
	}
	kfree(as->text_table);
	kfree(as->data_table);
	if(as->stack_table != NULL){
		kfree(as->stack_table);
	}
	kfree(as);
}

void
as_activate(void)
{
	int i, spl;
	struct addrspace *as;

	as = curproc_getas();
#ifdef UW
        /* Kernel threads don't have an address spaces to activate */
#endif
	if (as == NULL) {
		return;
	}

	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();

	for (i=0; i<NUM_TLB; i++) {
		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}

	splx(spl);
}

void
as_deactivate(void)
{
	/* nothing */
}

int
as_define_region(struct addrspace *as, vaddr_t vaddr, size_t sz,
		 int readable, int writeable, int executable)
{
	size_t npages; 

	/* Align the region. First, the base... */
	sz += vaddr & ~(vaddr_t)PAGE_FRAME;
	vaddr &= PAGE_FRAME;

	/* ...and now the length. */
	sz = (sz + PAGE_SIZE - 1) & PAGE_FRAME;

	npages = sz / PAGE_SIZE;

	(void)readable;
	(void)writeable;
	(void)executable;

	if (as->as_vbase1 == 0) {
		as->as_vbase1 = vaddr;
		as->as_npages1 = npages;
		as->text_table = kmalloc(npages * sizeof(struct PAGE_E));
		return 0;
	}

	if (as->as_vbase2 == 0) {
		as->as_vbase2 = vaddr;
		as->as_npages2 = npages;
		as->data_table = kmalloc(npages * sizeof(struct PAGE_E));
		return 0;
	}
	
	/*
	 * Support for more than two regions is not available.
	 */
	kprintf("dumbvm: Warning: too many regions\n");
	return EUNIMP;
}

static
void
as_zero_region(paddr_t paddr, unsigned npages)
{
	bzero((void *)PADDR_TO_KVADDR(paddr), npages * PAGE_SIZE);
}

int
as_prepare_load(struct addrspace *as)
{
	/*KASSERT(as->text_table != NULL);
	KASSERT(as->data_table != NULL);
	KASSERT(as->as_stackpbase == 0);
	*/

	as->stack_table = kmalloc(sizeof(struct PAGE_E) * DUMBVM_STACKPAGES);
	

	for(unsigned i = 0; i < as->as_npages1; i++){
		as->text_table[i].phys_addr = getppages(1);
		as_zero_region(as->text_table[i].phys_addr, 1);
	}
	//if (as->as_pbase1 == 0) {
	//	return ENOMEM;
	//}
	

	for(unsigned i = 0; i < as->as_npages2; i++){
		as->data_table[i].phys_addr = getppages(1);
		as_zero_region(as->data_table[i].phys_addr, 1);
	}
	
	/*as_zero_region(as->text_table[0].phys_addr, as->as_npages1 * PAGE_SIZE);
	as_zero_region(as->data_table[0].phys_addr, as->as_npages2 * PAGE_SIZE);
	as_zero_region(as->stack_table[0].phys_addr, DUMBVM_STACKPAGES * PAGE_SIZE);
	*/
	for(int i = 0; i < DUMBVM_STACKPAGES; i++){
			as->stack_table[i].phys_addr = getppages(1);
			as_zero_region(as->stack_table[i].phys_addr, 1);
	}
	return 0;
}

int
as_complete_load(struct addrspace *as)
{
	(void)as;
	return 0;
}

int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
	//KASSERT(as->as_stackpbase != 0);
	(void)as;
	*stackptr = USERSTACK;
	return 0;
}

int
as_copy(struct addrspace *old, struct addrspace **ret)
{
	struct addrspace *new;

	new = as_create();

	new->as_vbase1 = old->as_vbase1;
	new->as_npages1 = old->as_npages1;
	new->as_vbase2 = old->as_vbase2;
	new->as_npages2 = old->as_npages2;
	new->text_table = kmalloc(sizeof(struct PAGE_E)*old->as_npages1);
	new->data_table = kmalloc(sizeof(struct PAGE_E)*old->as_npages2);
	new->stack_table = kmalloc(sizeof(struct PAGE_E)*DUMBVM_STACKPAGES);
	/* (Mis)use as_prepare_load to allocate some physical memory. */

	if (as_prepare_load(new)) {
		as_destroy(new);
		return ENOMEM;
	}
	for(unsigned i = 0; i < old->as_npages1; i++){
		memmove((void *)PADDR_TO_KVADDR(new->text_table[i].phys_addr),
			(const void *)PADDR_TO_KVADDR(old->text_table[i].phys_addr),
			PAGE_SIZE);
	}
	
	for(unsigned i = 0; i < old->as_npages2; i++){
		memmove((void *)PADDR_TO_KVADDR(new->data_table[i].phys_addr),
			(const void *)PADDR_TO_KVADDR(old->data_table[i].phys_addr),
			PAGE_SIZE);
	}
	if(old->stack_table){
		
		for(unsigned i = 0; i < DUMBVM_STACKPAGES; i++){
			memmove((void *)PADDR_TO_KVADDR(new->stack_table[i].phys_addr),
				(const void *)PADDR_TO_KVADDR(old->stack_table[i].phys_addr),
				PAGE_SIZE);
		}
	}
	
	
	*ret = new;
	return 0;
}
