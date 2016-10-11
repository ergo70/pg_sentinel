# pg_sentinel Makefile

MODULE_big = pg_sentinel
OBJS = pg_sentinel.o $(WIN32RES)
PGFILEDESC = "filter result sets by sentinel value"
DOCS         = $(wildcard doc/*.md)

PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
