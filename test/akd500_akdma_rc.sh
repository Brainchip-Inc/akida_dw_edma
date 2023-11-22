#! /bin/sh

AKIDA_DEV=/dev/akd1500_0

MYDIR=`dirname $(realpath $0)`

akd_write32() {
	echo "Write32 @$1 $2"
	$MYDIR/mmap_access $AKIDA_DEV $1 32 $2
}

akd_read32() {
	val=$($MYDIR/mmap_access $AKIDA_DEV $1 32)
	echo "Read32 @$1 $val"
}


## Test EP to RC using Akida DMA

# AKD1500 DMA Reset, issued from RC
akd_read32 0xfcc20000
akd_read32 0xfcc20004
akd_read32 0xfcc20008
akd_read32 0xfcc2000c
akd_write32 0xfcc20000 0x83000200
# Add Delay for rest to finish
akd_read32 0xfcc20000
akd_read32 0xfcc20004
akd_read32 0xfcc20008
akd_read32 0xfcc2000c
# AKD1500 DMA Setup. Enable DMA and choose mode of operation
akd_write32 0xfcc20000 0x83000502
# DMA Descriptor Base Address. This should point to the starting location of the DMA Descriptors
akd_write32 0xfcc20008 0xc0001000
# DMA Loopback. DMA Ibound Channel is Looped back to Outbound Channel before packet formating. This behaves like a pure data transfer DMA.
# In Loopback mode Inbound (Source) and Outbound (Destination) Data should match in this mode 
akd_write32 0xfcc200b0 0x00000001

## DMA Descriptor in RC Memory Space. Read payload from RC address 0x10002000 and write output at RC address 0x10003000
## Note that the payload inbound and outbound addresses in Word#2 & 4 of descriptor (at 0x10001004 and 0x1000100c) is relative to EP address map 
## If more than one descriptors needed, the next one starts at offset 0x40 from Descriptor Base adddress, and so on...
# Word#1 is for internal use. Keep 0
akd_write32 0xc0001000 0
# Word#2 is the payload's source address. Since this will be initated from the AKD1500 DMA, it should be the translated address to EP space 
akd_write32 0xc0001004 0xc0002000
# Specifies the number of 32b data to transfer
akd_write32 0xc0001008 4
# Specifies the Destination address. Since this will be initated from the AKD1500 DMA, it should be the translated address to EP space 
akd_write32 0xc000100c 0xc0003000

# Source Data in RC memory. Starting Address is the one specified in word#2 of DMA descriptor (at 0x10001004)
akd_write32 0xc0002000 0xbabeface
akd_write32 0xc0002004 0xdeadc0de
akd_write32 0xc0002008 0xabcdef01
akd_write32 0xc000200c 0x12345678

#Clear OB memory space. Initial value for reference when comapring source and destination data.
akd_write32 0xc0003000 0
akd_write32 0xc0003020 0
akd_write32 0xc0003024 0
akd_write32 0xc0003028 0
akd_write32 0xc000302c 0
#Read RC memory
akd_read32 0xc0003000
akd_read32 0xc0003020
akd_read32 0xc0003024
akd_read32 0xc0003028
akd_read32 0xc000302c


# Advance EP AK DMA Descriptor. Upper 16bits specify the descriptor to execute, 0 being descriptor #0 at base address, 1 begin descriptor #1 at base address + 0x40, ....
# In this instance, only one descriptor is executed (descriptor #0)
akd_write32 0xfcc20004 00000000

#Check EP DMA Status. A value 0x3 in lower bits indicate that the transfer is complete
akd_read32 0xfcc20024
akd_read32 0xfcc20028

#Read RC memory at. Source data should overwrite the initial 0 data 
akd_read32 0xc0003000
akd_read32 0xc0003020
akd_read32 0xc0003024
akd_read32 0xc0003028
akd_read32 0xc000302c

