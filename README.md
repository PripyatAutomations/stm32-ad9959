# stm32-ad9959
Small C program for posix-compliant hosts to control the cheap ch*nese AD9959 + stm32 dev boards

It's planned to try to dump the firmware and decompile it.

If you have one of these boards, this might be of use to you.

Right now the following commands are supported:

* name	min/max args	Description
help		0, 0	This help message
amp		0, 1	Show/set amplitude [0-1023]
chan		0, 1	Show/set channel [1-4]
debug		0, 1	Show/set debug level [0-10]
factory		1, 1	Restore factory settings (must pass CONFIRM as arg!)
freq		0, 1	Show/set frequency [1-200,000,000] Hz
info		0, 1	Show board information
load		0, 1	Load settings from stdin or file
mode		0, 1	Show/set mode [POINT|SWEEP|FSK2|FSK4|AM]
mult		0, 1	Show/set multiplier [1-20]
phase		0, 1	Show/set phase [0-16383 corresponding to 0-360 deg]
quit		0, 0	Exit the program
ref		0, 1	Show/set refclk freq [10,000,000-125,000,000] Hz
reset		0, 0	Reset the board
save		0, 1	Save the settings to stdout or file
enda		0, 1	Show/set sweep END amplitude [0-1023]
endf		0, 1	Show/set sweep END frequency [STARTFRE-200,000,000]
starta		0, 1	Show/set sweep START amplitude [0-1023]
startf		0, 1	Show/set sweep START frequency [1-ENDFRE]
step		0, 1	Show/set sweep STEP interval [1-200,000,000] Hz
sweep		0, 1	Show/set sweep status [ON|OFF]
time		0, 1	Show/set sweep time [1-9999] ms
ver		0, 0	Show firmware version
****
