OBJS:=$(OBJS) $(subst .c,.o,$(SRCS))
OBJS:=$(sort $(OBJS))
SUBDIRS:=$(sort $(SUBDIRS))

.c.o :
	gcc $(CFLAGS) $(INCLUDES) -c -o $*.o $<


$(TARGET) : $(OBJS)
	gcc $(OBJS) $(LDFLAGS) -o $@

clean:
	rm -f $(TARGET) $(OBJS) $(CLEAN_TARGETS)

	@for d in $(SUBDIRS) ; do make -c $$d clean ; done

dep:
	./tools/dep.py $(INCLUDES) -- $(SRCS)
	@for d in $(SUBDIRS) ; do make -c $$d dep ; done

distclean: clean
	rm -f .depend packs.make


ifneq ($(wildcard .depend),)
include .depend
endif