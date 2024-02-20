# stm32-ad9959
Small C program for posix-compliant hosts to control the cheap ch*nese AD9959 + stm32 dev boards which look like this:

![devboard.jpg](https://github.com/pripyatautomations/stm32-ad9959/blob/main/doc/board-001.jpg?raw=true)

Usually they'll say something about "AT Command" in the listing....

It's planned to try to dump the firmware and decompile it.

If you have one of these boards, this might be of use to you.

# Supported Commands
 	. Frequencies can entered as hz or decimal with suffix ie: 146.52m
	. Powers can be entered as 12.3%
	. Phase angles are in decimal degrees ie: 90.0

	 name	        args	 Description
	amp		0, 1	Show/set amplitude [0-1023]
	chan		0, 1	Show/set channel [1-4]
	debug		0, 1	Show/set debug level [0-10]
	factory		1, 1	Restore factory settings (must pass CONFIRM as arg!)
	freq		0, 1	Show/set frequency [1-200,000,000] Hz
	help		0, 0	This help message
	info		0, 1	Show board information
	load		0, 1	Load settings from stdin or file
	mode		0, 1	Show/set mode [POINT|SWEEP|FSK2|FSK4|AM]
	mult		0, 1	Show/set multiplier [1-20]
	phase		0, 1	Show/set phase [0-16383 corresponding to 0-360 deg]
	quit		0, 0	Exit the program
	ref		0, 1	Show/set refclk freq [10,000,000-125,000,000] Hz
	reset		0, 0	Reset the board
	save		0, 1	Save the settings to stdout or file
	endpower	0, 1	Show/set sweep END power [0-1023]
	endfreq		0, 1	Show/set sweep END frequency [STARTFRE-200,000,000]
	startpower	0, 1	Show/set sweep START power [0-1023]
	startfreq	0, 1	Show/set sweep START frequency [1-ENDFRE]
	step		0, 1	Show/set sweep STEP interval [1-200,000,000] Hz
	sweep		0, 1	Show/set sweep status [ON|OFF]
	time		0, 1	Show/set sweep time [1-9999] ms
	ver		0, 0	Show firmware version

# FSK/AM/PM
The stm32 isn't hooked to the p1-p4 pins needed to drive 16 level modes...

# Overall
If you haven't already purchased it, I'd avoid this board... Far better
options with the ad9959, especially on a faster bus!

# ToDo
MODE is global apparently, not per channel ;(
