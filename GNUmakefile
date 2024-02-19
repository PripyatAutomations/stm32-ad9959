CFLAGS := -ggdb -Wall -pedantic
LDFLAGS := -ggdb -lreadline -lev
port := /dev/ttyACM0

bin := freqgen
objs += freqgen.o
all: world

world: ${bin}

${bin}: ${objs}
	${CC} -o $@ $^ ${LDFLAGS}

%.o:%.c
	${CC} ${CFLAGS} -o $@ -c $<

clean:
	${RM} -f ${bin} ${objs}

gdb:
	gdb ${bin} -ex run

picocom:
	picocom -b 115200 --omap crcrlf ${port}
