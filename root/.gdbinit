dir ../os161-1.99/kern/compile/ASST2
target remote unix:.sockets/gdb
#break dumbvm.c: 141
break getppages
break vm_bootstrap
#break mips_trap
#break boot
#break load_elf
#break load_segment
break as_copy
break as_destroy
break as_define_region
break as_prepare_load
break panic
