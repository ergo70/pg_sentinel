# pg_sentinel Makefile

MODULE_big = pg_sentinel
OBJS = pg_sentinel.o $(WIN32RES)
PGFILEDESC = "Abort SELECT when sentinel value is emitted"
#DOCS         = $(wildcard doc/*.md)

PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
