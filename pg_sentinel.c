/*-------------------------------------------------------------------------
 *
 * pg_sentinel.c
 *
 * Loadable PostgreSQL module to abort queries if a certain sentinel value
 * is SELECTed. E.g. in case of a SQL injection attack.
 *
 * Copyright 2016 Ernst-Georg Schmid
 *
 * Distributed under The PostgreSQL License
 * see License file for terms
 *-------------------------------------------------------------------------
 */

#include "postgres.h"
#include "fmgr.h"
#include "funcapi.h"
#include "executor/executor.h"
#include "executor/spi.h"
#include "access/xact.h"
#include "utils/memutils.h"
#include "utils/guc.h"

PG_MODULE_MAGIC;

static bool abort_statement_only = false;
static int relation_oid;
static int col_no;
static int elevel = FATAL;
static char *sentinel_value;

static ExecutorRun_hook_type prev_ExecutorRun_hook = NULL;

static void sentinel_ExecutorRun(QueryDesc *queryDesc,
                                 ScanDirection direction, uint64 count);

void		_PG_init(void);
void		_PG_fini(void);

static void
ExecutePlan(EState *estate,
            PlanState *planstate,
            bool use_parallel_mode,
            CmdType operation,
            bool sendTuples,
            uint64 numberTuples,
            ScanDirection direction,
            DestReceiver *dest)
{
    TupleTableSlot *slot;
    HeapTuple tuple;
    uint64		current_tuple_count;

    /*
     * initialize local variables
     */
    current_tuple_count = 0;

    /*
     * Set the direction.
     */
    estate->es_direction = direction;

    /*
     * If a tuple count was supplied, we must force the plan to run without
     * parallelism, because we might exit early.
     */
    if (numberTuples)
        use_parallel_mode = false;

    /*
     * If a tuple count was supplied, we must force the plan to run without
     * parallelism, because we might exit early.
     */
    if (use_parallel_mode)
        EnterParallelMode();

    /*
     * Loop until we've processed the proper number of tuples from the plan.
     */
    for (;;)
    {
        /* Reset the per-output-tuple exprcontext */
        ResetPerTupleExprContext(estate);

        /*
         * Execute the plan and obtain a tuple
         */
        slot = ExecProcNode(planstate);

        /*
         * if the tuple is null, then we assume there is nothing more to
         * process so we just end the loop...
         */
        if (TupIsNull(slot))
        {
            /* Allow nodes to release or shut down resources. */
            (void) ExecShutdownNode(planstate);
            break;
        }

        /*
         * If we have a junk filter, then project a new tuple with the junk
         * removed.
         *
         * Store this new "clean" tuple in the junkfilter's resultSlot.
         * (Formerly, we stored it back over the "dirty" tuple, which is WRONG
         * because that tuple slot has the wrong descriptor.)
         */
        if (estate->es_junkFilter != NULL)
            slot = ExecFilterJunk(estate->es_junkFilter, slot);

        /*
         * If we are supposed to send the tuple somewhere, do so. (In
         * practice, this is probably always the case at this point.)
         */
        if (sendTuples)
        {
            /*
             * If we are not able to send the tuple, we assume the destination
             * has closed and no more tuples can be sent. If that's the case,
             * end the loop.
             */
            if (!((*dest->receiveSlot) (slot, dest)))
                break;
        }

        /*
         * Count tuples processed, if this is a SELECT.  (For other operation
         * types, the ModifyTable plan node must count the appropriate
         * events.)
         */
        if (operation == CMD_SELECT)
        {
            /* Inspect the current tuple.
             * If the column value equals the sentinel value
             * trigger a defensive action.
             */

            tuple = slot->tts_tuple;

            if(tuple != NULL && tuple->t_tableOid == relation_oid)
            {
                char *col_val = SPI_getvalue(tuple, slot->tts_tupleDescriptor, col_no);
                if(strncmp(sentinel_value,col_val,strlen(sentinel_value)) == 0)
                    ereport(elevel, (errmsg("Severe internal error detected!"))); /* ERROR - terminate the statement. FATAL - terminate the connection. */
            }
            (estate->es_processed)++;
        }

        /*
         * check our tuple count.. if we've processed the proper number then
         * quit, else loop again and process more tuples.  Zero numberTuples
         * means no limit.
         */
        current_tuple_count++;
        if (numberTuples && numberTuples == current_tuple_count)
            break;
    }

    if (use_parallel_mode)
        ExitParallelMode();
}


/*
 * Module load callback
 */
void
_PG_init(void)
{
    /* Define custom GUC variable. */
    DefineCustomIntVariable("pg_sentinel.relation_oid",
                            "Selects the table by Oid "
                            "that contains the sentinel value.",
                            "Oid can be determinded with: SELECT '<schema>.<tablename>'::regclass::oid;",
                            &relation_oid,
                            0,
                            0, INT_MAX,
                            PGC_POSTMASTER,
                            0, /* no flags required */
                            NULL,
                            NULL,
                            NULL);

    /* Define custom GUC variable. */
    DefineCustomIntVariable("pg_sentinel.column_no",
                            "Sets the column position in the table "
                            "which contains the sentinel value.",
                            "Column position can be determined by: SELECT ordinal_position FROM information_schema.columns WHERE table_name='<tablename>' AND column_name = '<column_name>';",
                            &col_no,
                            0,
                            0, INT_MAX,
                            PGC_POSTMASTER,
                            0, /* no flags required */
                            NULL,
                            NULL,
                            NULL);

    /* Define custom GUC variable. */
    DefineCustomStringVariable("pg_sentinel.sentinel_value",
                               "Sets the sentinel value that "
                               "triggers abort.",
                               "Default: 'SENTINEL'",
                               &sentinel_value,
                               "SENTINEL",
                               PGC_POSTMASTER,
                               0, /* no flags required */
                               NULL,
                               NULL,
                               NULL);

    /* Define custom GUC variable. */
    DefineCustomBoolVariable("pg_sentinel.abort_statement_only",
                             "Controls if only the statement "
                             "or the connection is aborted.",
                             "Default: Connection abort.",
                             &abort_statement_only,
                             false,
                             PGC_POSTMASTER,
                             0, /* no flags required */
                             NULL,
                             NULL,
                             NULL);

    /* install the hook */
    prev_ExecutorRun_hook = ExecutorRun_hook;
    ExecutorRun_hook = sentinel_ExecutorRun;

    if (abort_statement_only)
    {
        elevel = ERROR;
    }
}

/*
 * Module unload callback
 */
void
_PG_fini(void)
{
    /* Uninstall hook. */
    ExecutorRun_hook = prev_ExecutorRun_hook;
}

void
sentinel_ExecutorRun(QueryDesc *queryDesc,
                     ScanDirection direction, uint64 count)
{
    EState	   *estate;
    CmdType		operation;
    DestReceiver *dest;
    bool		sendTuples;
    MemoryContext oldcontext;

    /* sanity checks */
    Assert(queryDesc != NULL);

    estate = queryDesc->estate;

    Assert(estate != NULL);
    Assert(!(estate->es_top_eflags & EXEC_FLAG_EXPLAIN_ONLY));

    /*
     * Switch into per-query memory context
     */
    oldcontext = MemoryContextSwitchTo(estate->es_query_cxt);

    /* Allow instrumentation of Executor overall runtime */
    if (queryDesc->totaltime)
        InstrStartNode(queryDesc->totaltime);

    /*
     * extract information from the query descriptor and the query feature.
     */
    operation = queryDesc->operation;
    dest = queryDesc->dest;

    /*
     * startup tuple receiver, if we will be emitting tuples
     */
    estate->es_processed = 0;
    estate->es_lastoid = InvalidOid;

    sendTuples = (operation == CMD_SELECT ||
                  queryDesc->plannedstmt->hasReturning);

    if (sendTuples)
        (*dest->rStartup) (dest, operation, queryDesc->tupDesc);

    /*
     * run plan
     */
    if (!ScanDirectionIsNoMovement(direction))
        ExecutePlan(estate,
                    queryDesc->planstate,
                    queryDesc->plannedstmt->parallelModeNeeded,
                    operation,
                    sendTuples,
                    count,
                    direction,
                    dest);

    /*
     * shutdown tuple receiver, if we started it
     */
    if (sendTuples)
        (*dest->rShutdown) (dest);

    if (queryDesc->totaltime)
        InstrStopNode(queryDesc->totaltime, estate->es_processed);

    MemoryContextSwitchTo(oldcontext);
}
