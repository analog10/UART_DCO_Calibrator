#
 # Makefile for msp430
 #
 # 'make' builds everything
 # 'make clean' deletes everything except source files and Makefile
 # You need to set TARGET, MCU and SOURCES for your project.
 # TARGET is the name of the executable file to be produced 
 # $(TARGET).elf $(TARGET).hex and $(TARGET).txt and $(TARGET).map are all generated.
 # The TXT file is used for BSL loading, the ELF can be used for JTAG use
 # 
 TARGET     = UART_CALIBRATE
 MCU        = msp430g2231
 # List all the source files here
 # eg if you have a source file foo.c then list it here
 SOURCES = main.c
 # Include are located in the Include directory
 INCLUDES = -I .
 # Add or subtract whatever MSPGCC flags you want. There are plenty more
 #######################################################################################
 CFLAGS   = -mmcu=$(MCU) -std=c99 -g -Os -Wall -Wunused -mnoint-hwmul -DF_CPU=1000000 $(INCLUDES)
 ASFLAGS  = -mmcu=$(MCU) -x assembler-with-cpp -Wa,-gstabs
 LDFLAGS  = -mmcu=$(MCU) -Wl,-Map=$(TARGET).map
 ########################################################################################
 HOST_CC  = gcc
 OBJDUMP  = msp430-objdump
 CC       = msp430-gcc
 LD       = msp430-ld
 AR       = msp430-ar
 AS       = msp430-gcc
 GASP     = msp430-gasp
 NM       = msp430-nm
 OBJCOPY  = msp430-objcopy
 RANLIB   = msp430-ranlib
 STRIP    = msp430-strip
 SIZE     = msp430-size
 READELF  = msp430-readelf
 NAKEN    = naken_util
 MAKETXT  = srec_cat
 CP       = cp -p
 RM       = rm -f
 MV       = mv
 ########################################################################################
 # the file which will include dependencies
 DEPEND = $(SOURCES:.c=.d)

 # all the object files
 OBJECTS = $(SOURCES:.c=.o)
 all: $(TARGET).elf $(TARGET).hex $(TARGET).txt $(TARGET).s  host_calibrate
 $(TARGET).elf: $(OBJECTS)
	echo "Linking $@"
	$(CC) $(OBJECTS) $(LDFLAGS) $(LIBS) -o $@
	echo
	echo ">>>> Size of Firmware <<<<"
	$(SIZE) $(TARGET).elf
	echo
 %.hex: %.elf
	$(OBJCOPY) -O ihex $< $@
 %.s: %.elf
	$(OBJDUMP) -d $< > $@
 %.txt: %.hex
	$(MAKETXT) -O $@ -TITXT $< -I

 cycles: $(TARGET).cycles.txt
 %.cycles.txt: %.hex
	$(NAKEN) -disasm $< > $@

 %.o: %.c
	echo "Compiling $<"
	$(CC) -c $(CFLAGS) -o $@ $<
 # rule for making assembler source listing, to see the code
 %.lst: %.c
	$(CC) -c $(ASFLAGS) -Wa,-anlhd $< > $@
 # include the dependencies unless we're going to clean, then forget about them.
 ifneq ($(MAKECMDGOALS), clean)
 -include $(DEPEND)
 endif
 # dependencies file
 # includes also considered, since some of these are our own
 # (otherwise use -MM instead of -M)
 %.d: %.c
	echo "Generating dependencies $@ from $<"
	$(CC) -M ${CFLAGS} $< >$@
 .SILENT:
 .PHONY:	clean

 host_calibrate: host_calibrate.c protocol.h
	$(HOST_CC) host_calibrate.c -g -o host_calibrate -lm

 clean:
	-$(RM) $(OBJECTS)
	-$(RM) $(TARGET).*
#	-$(RM) $(SOURCES:.c=.lst)
	-$(RM) $(DEPEND)
	-$(RM) host_calibrate
