TARGETS = job enq deq stat demo
all: $(TARGETS)

%: %.c job.h
	cc -o $@ $<

clean:
	rm -f $(TARGETS)

.PHONY: all clean
