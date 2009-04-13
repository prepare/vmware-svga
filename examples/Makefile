SUBDIRS = $(subst /Makefile,,$(wildcard */Makefile))

.PHONY: subdirs clean $(SUBDIRS)

subdirs: $(SUBDIRS)

$(SUBDIRS):
	$(MAKE) -C $@

clean:
	for dir in $(SUBDIRS); do $(MAKE) -C $$dir clean; done
