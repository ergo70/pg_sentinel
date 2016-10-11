Sentinel Value Module for PostgreSQL
===========================================

This loadable module will test tuples emitted from a SELECT statement against 
the occurrence of a configured sentinel value during execution, 
raising a terminal error if one is seen. This allows to add another layer of 
defence against unsolicited access, e.g. by SQL injection.

The module is loaded via the `shared_preload_libraries` setting in the 
`postgresql.conf` file.

Also, a few custom variables must be specified.

So, typical settings in the `postgresql.conf` file might be:

    shared_preload_libraries = 'pg_sentinel'
    pg_sentinel.relation_oid = 16389
    pg_sentinel.column_no = 2
    pg_sentinel.sentinel_value = 'SENTINEL'
    pg_sentinel.abort_statement_only = false

`relation_oid` is the Oid of the table containing the sentinel values.

The Oid can be obtained as follows:

    SELECT '<schema>.<tablename>'::regclass::oid;

`column_no` is the ordinal position of the column containing the sentinel values.

The ordinal position of a column can be obtained as follows:

    SELECT ordinal_position FROM information_schema.columns WHERE
    table_name='<table_name>' AND column_name = '<column_name>';

`sentinel_value` is the sentinel value to react to. The default is 'SENTINEL'.

If `abort_statement_only` is `true`, pg_sentinel will raise an `ERROR`, aborting
the current query. By default it is `false`, terminating the current connection
with `FATAL`.

If you're using this with a version of PostgreSQL prior to 9.2, you will 
need also to have a line like this before the above lines:

    custom_variable_classes = 'pg_sentinel'

All settings can only be set in postgresql.conf and only at startup.
They must not and can not be changed a posteriori by SET or SIGHUP to
avoid tampering.

Building and Installing
-----------------------

No configuration is required. Building and installing can be achieved
like this:

    export PATH=/path/to/pgconfig/directory:$PATH
    make && make install
    
As `pg_sentinel` is a loadable module rather than an Extension, it cannot be
installed using PGXN or other extension-management tools.

This module has been tested on PostgreSQL 9.6.  Since it implements it's own
`ExecutePlan()` function, it might work on other versions - or not.

Credits
-------

This module is the work of Ernst-Georg Schmid.
