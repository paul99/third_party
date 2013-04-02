/*
 * Copyright (C) 2007, 2008, 2013 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 3.  Neither the name of Apple Computer, Inc. ("Apple") nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "SQLTransactionBackend.h"

#if ENABLE(SQL_DATABASE)

#include "Database.h"
#include "DatabaseAuthorizer.h"
#include "DatabaseBackendContext.h"
#include "DatabaseThread.h"
#include "ExceptionCode.h"
#include "Logging.h"
#include "SQLError.h"
#include "SQLStatement.h"
#include "SQLStatementCallback.h"
#include "SQLStatementErrorCallback.h"
#include "SQLTransaction.h"
#include "SQLTransactionCallback.h"
#include "SQLTransactionClient.h"
#include "SQLTransactionCoordinator.h"
#include "SQLTransactionErrorCallback.h"
#include "SQLValue.h"
#include "SQLiteTransaction.h"
#include "ScriptExecutionContext.h"
#include "VoidCallback.h"
#include <wtf/OwnPtr.h>
#include <wtf/PassRefPtr.h>
#include <wtf/RefPtr.h>
#include <wtf/text/WTFString.h>


// How does a SQLTransaction work?
// ==============================
// The SQLTransaction is a state machine that executes a series of states / steps.
//
// ths State Transition Graph at a glance:
// ======================================
//
//    Backend (works with SQLiteDatabase)          .   Frontend (works with Script)
//    ===================================          .   ============================
//   ,--> State 0: [initial state]                 .
//   | ^     v                                     .
//   | |  State 1: [acquireLock]                   .
//   | |     v                                     .
//   | |  State 2: [openTransactionAndPreflight] ----------------------------------------------------.
//   | |     |                                     .                                                 |
//   | |     `---------------------------------------> State 3: [deliverTransactionCallback] -----.  |
//   | |                                           .      |                                       v  v
//   | |     ,--------------------------------------------'     State 10: [deliverTransactionErrorCallback] +
//   | |     |                                     .                                              ^  ^  ^   |
//   | |     v                                     .                                              |  |  |   |
//   | |  State 4: [runStatements] ---------------------------------------------------------------'  |  |   |
//   | |     |        ^  ^ |  ^ |                  .                                                 |  |   |
//   | |     |--------'  | |  | `--------------------> State 8: [deliverStatementCallback] +---------'  |   |
//   | |     |           | |  `------------------------------------------------------------'            |   |
//   | |     |           | `-------------------------> State 9: [deliverQuotaIncreaseCallback] +        |   |
//   | |     |            `--------------------------------------------------------------------'        |   |
//   | |     v                                     .                                                    |   |
//   | |  State 5: [postflightAndCommit] --+------------------------------------------------------------'   |
//   | |                                   |---------> State 6: [deliverSuccessCallback] +                  |
//   | |     ,-----------------------------'       .                                     |                  |
//   | |     v                                     .                                     |                  |
//   | |  State 7: [cleanupAfterSuccessCallback] <---------------------------------------'                  |
//   | `-----'                                     .                                                        |
//   `------------------------------------------------------------------------------------------------------'
//                                                 .
//
// the States and State Transitions:
// ================================
// Executed in the back-end:
//     State 0: [initial state]
//     - On scheduled transaction, goto [acquireLock].
//
//     State 1: [acquireLock]
//     - acquire lock.
//     - on "lock" acquisition, goto [openTransactionAndPreflight].
//
//     State 2: [openTransactionAndPreflight]
//     - Sets up an SQLiteTransaction.
//     - begin the SQLiteTransaction.
//     - call the SQLTransactionWrapper preflight if available.
//     - schedule script callback.
//     - on error (see handleTransactionError), goto [deliverTransactionErrorCallback].
//     - goto [deliverTransactionCallback].
//
// Executed in the front-end:
//     State 3: [deliverTransactionCallback]
//     - invoke the script function callback() if available.
//     - on error, goto [deliverTransactionErrorCallback].
//     - goto [runStatements].
//
// Executed in the back-end:
//     State 4: [runStatements]
//     - while there are statements {
//         - run a statement.
//         - if statementCallback is available, goto [deliverStatementCallback].
//         - on error,
//           goto [deliverQuotaIncreaseCallback], or
//           goto [deliverStatementCallback] (see handleCurrentStatementError), or
//           goto [deliverTransactionErrorCallback].
//       }
//     - goto [postflightAndCommit].
//
//     State 5: [postflightAndCommit]
//     - call the SQLTransactionWrapper postflight if available.
//     - commit the SQLiteTansaction.
//     - on error, goto [deliverTransactionErrorCallback] (see handleTransactionError).
//     - if successCallback is available, goto [deliverSuccessCallback].
//       else goto [cleanupAfterSuccessCallback].
//
// Executed in the front-end:
//     State 6: [deliverSuccessCallback]
//     - invoke the script function successCallback() if available.
//     - goto [cleanupAfterSuccessCallback].
//
// Executed in the back-end:
//     State 7: [cleanupAfterSuccessCallback]
//     - clean the SQLiteTransaction.
//     - release lock.
//     - goto [initial state].
//
// Other states:
// Executed in the front-end:
//     State 8: [deliverStatementCallback]
//     - invoke script statement callback (assume available).
//     - on error (see handleTransactionError),
//       goto [deliverTransactionErrorCallback].
//     - goto [runStatements].
//
//     State 9: [deliverQuotaIncreaseCallback]
//     - give client a chance to increase the quota.
//     - goto [runStatements].
//
//     State 10: [deliverTransactionErrorCallback]
//     - invoke the script function errorCallback if available.
//     - goto [cleanupAfterTransactionErrorCallback].
//
// Executed in the back-end:
//     State 11: [cleanupAfterTransactionErrorCallback]
//     - rollback and clear SQLiteTransaction.
//     - clear statements.
//     - release lock.
//     - goto [initial state].


// There's no way of knowing exactly how much more space will be required when a statement hits the quota limit.
// For now, we'll arbitrarily choose currentQuota + 1mb.
// In the future we decide to track if a size increase wasn't enough, and ask for larger-and-larger increases until its enough.
static const int DefaultQuotaSizeIncrease = 1048576;

namespace WebCore {

SQLTransactionBackend::SQLTransactionBackend(Database* db, PassRefPtr<SQLTransactionCallback> callback,
    PassRefPtr<SQLTransactionErrorCallback> errorCallback, PassRefPtr<VoidCallback> successCallback,
    PassRefPtr<SQLTransactionWrapper> wrapper, bool readOnly)
    : m_nextStep(&SQLTransactionBackend::acquireLock)
    , m_executeSqlAllowed(false)
    , m_database(db)
    , m_wrapper(wrapper)
    , m_callbackWrapper(callback, db->scriptExecutionContext())
    , m_successCallbackWrapper(successCallback, db->scriptExecutionContext())
    , m_errorCallbackWrapper(errorCallback, db->scriptExecutionContext())
    , m_shouldRetryCurrentStatement(false)
    , m_modifiedDatabase(false)
    , m_lockAcquired(false)
    , m_readOnly(readOnly)
    , m_hasVersionMismatch(false)
{
    ASSERT(m_database);
}

SQLTransactionBackend::~SQLTransactionBackend()
{
    ASSERT(!m_sqliteTransaction);
}

void SQLTransactionBackend::executeSQL(const String& sqlStatement, const Vector<SQLValue>& arguments, PassRefPtr<SQLStatementCallback> callback, PassRefPtr<SQLStatementErrorCallback> callbackError, ExceptionCode& e)
{
    if (!m_executeSqlAllowed || !m_database->opened()) {
        e = INVALID_STATE_ERR;
        return;
    }

    int permissions = DatabaseAuthorizer::ReadWriteMask;
    if (!m_database->databaseContext()->allowDatabaseAccess())
        permissions |= DatabaseAuthorizer::NoAccessMask;
    else if (m_readOnly)
        permissions |= DatabaseAuthorizer::ReadOnlyMask;

    RefPtr<SQLStatement> statement = SQLStatement::create(m_database.get(), sqlStatement, arguments, callback, callbackError, permissions);

    if (m_database->deleted())
        statement->setDatabaseDeletedError(m_database.get());

    enqueueStatement(statement);
}

void SQLTransactionBackend::enqueueStatement(PassRefPtr<SQLStatement> statement)
{
    MutexLocker locker(m_statementMutex);
    m_statementQueue.append(statement);
}

#if !LOG_DISABLED
const char* SQLTransactionBackend::debugStepName(SQLTransactionBackend::TransactionStepMethod step)
{
    if (step == &SQLTransactionBackend::acquireLock)
        return "acquireLock";
    if (step == &SQLTransactionBackend::openTransactionAndPreflight)
        return "openTransactionAndPreflight";
    if (step == &SQLTransactionBackend::runStatements)
        return "runStatements";
    if (step == &SQLTransactionBackend::postflightAndCommit)
        return "postflightAndCommit";
    if (step == &SQLTransactionBackend::cleanupAfterTransactionErrorCallback)
        return "cleanupAfterTransactionErrorCallback";
    if (step == &SQLTransactionBackend::deliverTransactionCallback)
        return "deliverTransactionCallback";
    if (step == &SQLTransactionBackend::deliverTransactionErrorCallback)
        return "deliverTransactionErrorCallback";
    if (step == &SQLTransactionBackend::deliverStatementCallback)
        return "deliverStatementCallback";
    if (step == &SQLTransactionBackend::deliverQuotaIncreaseCallback)
        return "deliverQuotaIncreaseCallback";
    if (step == &SQLTransactionBackend::deliverSuccessCallback)
        return "deliverSuccessCallback";
    if (step == &SQLTransactionBackend::cleanupAfterSuccessCallback)
        return "cleanupAfterSuccessCallback";
    return "UNKNOWN";
}
#endif

void SQLTransactionBackend::checkAndHandleClosedOrInterruptedDatabase()
{
    if (m_database->opened() && !m_database->isInterrupted())
        return;

    // If the database was stopped, don't do anything and cancel queued work
    LOG(StorageAPI, "Database was stopped or interrupted - cancelling work for this transaction");
    MutexLocker locker(m_statementMutex);
    m_statementQueue.clear();
    m_nextStep = 0;

    // Release the unneeded callbacks, to break reference cycles.
    m_callbackWrapper.clear();
    m_successCallbackWrapper.clear();
    m_errorCallbackWrapper.clear();

    // The next steps should be executed only if we're on the DB thread.
    if (currentThread() != database()->databaseContext()->databaseThread()->getThreadID())
        return;

    // The current SQLite transaction should be stopped, as well
    if (m_sqliteTransaction) {
        m_sqliteTransaction->stop();
        m_sqliteTransaction.clear();
    }

    if (m_lockAcquired)
        m_database->transactionCoordinator()->releaseLock(this);
}


bool SQLTransactionBackend::performNextStep()
{
    LOG(StorageAPI, "Step %s\n", debugStepName(m_nextStep));

    ASSERT(m_nextStep == &SQLTransactionBackend::acquireLock
        || m_nextStep == &SQLTransactionBackend::openTransactionAndPreflight
        || m_nextStep == &SQLTransactionBackend::runStatements
        || m_nextStep == &SQLTransactionBackend::postflightAndCommit
        || m_nextStep == &SQLTransactionBackend::cleanupAfterSuccessCallback
        || m_nextStep == &SQLTransactionBackend::cleanupAfterTransactionErrorCallback);

    checkAndHandleClosedOrInterruptedDatabase();

    if (m_nextStep)
        (this->*m_nextStep)();

    // If there is no nextStep after performing the above step, the transaction is complete
    return !m_nextStep;
}

void SQLTransactionBackend::performPendingCallback()
{
    LOG(StorageAPI, "Callback %s\n", debugStepName(m_nextStep));

    ASSERT(m_nextStep == &SQLTransactionBackend::deliverTransactionCallback
        || m_nextStep == &SQLTransactionBackend::deliverTransactionErrorCallback
        || m_nextStep == &SQLTransactionBackend::deliverStatementCallback
        || m_nextStep == &SQLTransactionBackend::deliverQuotaIncreaseCallback
        || m_nextStep == &SQLTransactionBackend::deliverSuccessCallback);

    checkAndHandleClosedOrInterruptedDatabase();

    if (m_nextStep)
        (this->*m_nextStep)();
}

void SQLTransactionBackend::notifyDatabaseThreadIsShuttingDown()
{
    ASSERT(currentThread() == database()->databaseContext()->databaseThread()->getThreadID());

    // If the transaction is in progress, we should roll it back here, since this is our last
    // oportunity to do something related to this transaction on the DB thread.
    // Clearing m_sqliteTransaction invokes SQLiteTransaction's destructor which does just that.
    m_sqliteTransaction.clear();
}

void SQLTransactionBackend::acquireLock()
{
    m_database->transactionCoordinator()->acquireLock(this);
}

void SQLTransactionBackend::lockAcquired()
{
    m_lockAcquired = true;
    m_nextStep = &SQLTransactionBackend::openTransactionAndPreflight;
    LOG(StorageAPI, "Scheduling openTransactionAndPreflight immediately for transaction %p\n", this);
    m_database->scheduleTransactionStep(this, true);
}

void SQLTransactionBackend::openTransactionAndPreflight()
{
    ASSERT(!m_database->sqliteDatabase().transactionInProgress());
    ASSERT(m_lockAcquired);

    LOG(StorageAPI, "Opening and preflighting transaction %p", this);

    // If the database was deleted, jump to the error callback
    if (m_database->deleted()) {
        m_database->reportStartTransactionResult(1, SQLError::UNKNOWN_ERR, 0);
        m_transactionError = SQLError::create(SQLError::UNKNOWN_ERR, "unable to open a transaction, because the user deleted the database");
        handleTransactionError(false);
        return;
    }

    // Set the maximum usage for this transaction if this transactions is not read-only
    if (!m_readOnly)
        m_database->sqliteDatabase().setMaximumSize(m_database->maximumSize());

    ASSERT(!m_sqliteTransaction);
    m_sqliteTransaction = adoptPtr(new SQLiteTransaction(m_database->sqliteDatabase(), m_readOnly));

    m_database->resetDeletes();
    m_database->disableAuthorizer();
    m_sqliteTransaction->begin();
    m_database->enableAuthorizer();

    // Transaction Steps 1+2 - Open a transaction to the database, jumping to the error callback if that fails
    if (!m_sqliteTransaction->inProgress()) {
        ASSERT(!m_database->sqliteDatabase().transactionInProgress());
        m_database->reportStartTransactionResult(2, SQLError::DATABASE_ERR, m_database->sqliteDatabase().lastError());
        m_transactionError = SQLError::create(SQLError::DATABASE_ERR, "unable to begin transaction",
            m_database->sqliteDatabase().lastError(), m_database->sqliteDatabase().lastErrorMsg());
        m_sqliteTransaction.clear();
        handleTransactionError(false);
        return;
    }

    // Note: We intentionally retrieve the actual version even with an empty expected version.
    // In multi-process browsers, we take this opportinutiy to update the cached value for
    // the actual version. In single-process browsers, this is just a map lookup.
    String actualVersion;
    if (!m_database->getActualVersionForTransaction(actualVersion)) {
        m_database->reportStartTransactionResult(3, SQLError::DATABASE_ERR, m_database->sqliteDatabase().lastError());
        m_transactionError = SQLError::create(SQLError::DATABASE_ERR, "unable to read version",
            m_database->sqliteDatabase().lastError(), m_database->sqliteDatabase().lastErrorMsg());
        m_database->disableAuthorizer();
        m_sqliteTransaction.clear();
        m_database->enableAuthorizer();
        handleTransactionError(false);
        return;
    }
    m_hasVersionMismatch = !m_database->expectedVersion().isEmpty() && (m_database->expectedVersion() != actualVersion);

    // Transaction Steps 3 - Peform preflight steps, jumping to the error callback if they fail
    if (m_wrapper && !m_wrapper->performPreflight(SQLTransaction::from(this))) {
        m_database->disableAuthorizer();
        m_sqliteTransaction.clear();
        m_database->enableAuthorizer();
        m_transactionError = m_wrapper->sqlError();
        if (!m_transactionError) {
            m_database->reportStartTransactionResult(4, SQLError::UNKNOWN_ERR, 0);
            m_transactionError = SQLError::create(SQLError::UNKNOWN_ERR, "unknown error occurred during transaction preflight");
        }
        handleTransactionError(false);
        return;
    }

    // Transaction Step 4 - Invoke the transaction callback with the new SQLTransaction object
    m_nextStep = &SQLTransactionBackend::deliverTransactionCallback;
    LOG(StorageAPI, "Scheduling deliverTransactionCallback for transaction %p\n", this);
    m_database->scheduleTransactionCallback(SQLTransaction::from(this));
}

void SQLTransactionBackend::deliverTransactionCallback()
{
    bool shouldDeliverErrorCallback = false;

    RefPtr<SQLTransactionCallback> callback = m_callbackWrapper.unwrap();
    if (callback) {
        m_executeSqlAllowed = true;
        shouldDeliverErrorCallback = !callback->handleEvent(SQLTransaction::from(this));
        m_executeSqlAllowed = false;
    }

    // Transaction Step 5 - If the transaction callback was null or raised an exception, jump to the error callback
    if (shouldDeliverErrorCallback) {
        m_database->reportStartTransactionResult(5, SQLError::UNKNOWN_ERR, 0);
        m_transactionError = SQLError::create(SQLError::UNKNOWN_ERR, "the SQLTransactionCallback was null or threw an exception");
        deliverTransactionErrorCallback();
    } else
        scheduleToRunStatements();

    m_database->reportStartTransactionResult(0, -1, 0); // OK
}

void SQLTransactionBackend::scheduleToRunStatements()
{
    m_nextStep = &SQLTransactionBackend::runStatements;
    LOG(StorageAPI, "Scheduling runStatements for transaction %p\n", this);
    m_database->scheduleTransactionStep(this);
}

void SQLTransactionBackend::runStatements()
{
    ASSERT(m_lockAcquired);

    // If there is a series of statements queued up that are all successful and have no associated
    // SQLStatementCallback objects, then we can burn through the queue
    do {
        if (m_shouldRetryCurrentStatement && !m_sqliteTransaction->wasRolledBackBySqlite()) {
            m_shouldRetryCurrentStatement = false;
            // FIXME - Another place that needs fixing up after <rdar://problem/5628468> is addressed.
            // See ::openTransactionAndPreflight() for discussion

            // Reset the maximum size here, as it was increased to allow us to retry this statement.
            // m_shouldRetryCurrentStatement is set to true only when a statement exceeds
            // the quota, which can happen only in a read-write transaction. Therefore, there
            // is no need to check here if the transaction is read-write.
            m_database->sqliteDatabase().setMaximumSize(m_database->maximumSize());
        } else {
            // If the current statement has already been run, failed due to quota constraints, and we're not retrying it,
            // that means it ended in an error. Handle it now
            if (m_currentStatement && m_currentStatement->lastExecutionFailedDueToQuota()) {
                handleCurrentStatementError();
                break;
            }

            // Otherwise, advance to the next statement
            getNextStatement();
        }
    } while (runCurrentStatement());

    // If runCurrentStatement() returned false, that means either there was no current statement to run,
    // or the current statement requires a callback to complete. In the later case, it also scheduled
    // the callback or performed any other additional work so we can return
    if (!m_currentStatement)
        postflightAndCommit();
}

void SQLTransactionBackend::getNextStatement()
{
    m_currentStatement = 0;

    MutexLocker locker(m_statementMutex);
    if (!m_statementQueue.isEmpty())
        m_currentStatement = m_statementQueue.takeFirst();
}

bool SQLTransactionBackend::runCurrentStatement()
{
    if (!m_currentStatement)
        return false;

    m_database->resetAuthorizer();

    if (m_hasVersionMismatch)
        m_currentStatement->setVersionMismatchedError(m_database.get());

    if (m_currentStatement->execute(m_database.get())) {
        if (m_database->lastActionChangedDatabase()) {
            // Flag this transaction as having changed the database for later delegate notification
            m_modifiedDatabase = true;
            // Also dirty the size of this database file for calculating quota usage
            m_database->transactionClient()->didExecuteStatement(database());
        }

        if (m_currentStatement->hasStatementCallback()) {
            m_nextStep = &SQLTransactionBackend::deliverStatementCallback;
            LOG(StorageAPI, "Scheduling deliverStatementCallback for transaction %p\n", this);
            m_database->scheduleTransactionCallback(SQLTransaction::from(this));
            return false;
        }
        return true;
    }

    if (m_currentStatement->lastExecutionFailedDueToQuota()) {
        m_nextStep = &SQLTransactionBackend::deliverQuotaIncreaseCallback;
        LOG(StorageAPI, "Scheduling deliverQuotaIncreaseCallback for transaction %p\n", this);
        m_database->scheduleTransactionCallback(SQLTransaction::from(this));
        return false;
    }

    handleCurrentStatementError();

    return false;
}

void SQLTransactionBackend::handleCurrentStatementError()
{
    // Transaction Steps 6.error - Call the statement's error callback, but if there was no error callback,
    // or the transaction was rolled back, jump to the transaction error callback
    if (m_currentStatement->hasStatementErrorCallback() && !m_sqliteTransaction->wasRolledBackBySqlite()) {
        m_nextStep = &SQLTransactionBackend::deliverStatementCallback;
        LOG(StorageAPI, "Scheduling deliverStatementCallback for transaction %p\n", this);
        m_database->scheduleTransactionCallback(SQLTransaction::from(this));
    } else {
        m_transactionError = m_currentStatement->sqlError();
        if (!m_transactionError) {
            m_database->reportCommitTransactionResult(1, SQLError::DATABASE_ERR, 0);
            m_transactionError = SQLError::create(SQLError::DATABASE_ERR, "the statement failed to execute");
        }
        handleTransactionError(false);
    }
}

void SQLTransactionBackend::deliverStatementCallback()
{
    ASSERT(m_currentStatement);

    // Transaction Step 6.6 and 6.3(error) - If the statement callback went wrong, jump to the transaction error callback
    // Otherwise, continue to loop through the statement queue
    m_executeSqlAllowed = true;
    bool result = m_currentStatement->performCallback(SQLTransaction::from(this));
    m_executeSqlAllowed = false;

    if (result) {
        m_database->reportCommitTransactionResult(2, SQLError::UNKNOWN_ERR, 0);
        m_transactionError = SQLError::create(SQLError::UNKNOWN_ERR, "the statement callback raised an exception or statement error callback did not return false");
        handleTransactionError(true);
    } else
        scheduleToRunStatements();
}

void SQLTransactionBackend::deliverQuotaIncreaseCallback()
{
    ASSERT(m_currentStatement);
    ASSERT(!m_shouldRetryCurrentStatement);

    m_shouldRetryCurrentStatement = m_database->transactionClient()->didExceedQuota(database());

    m_nextStep = &SQLTransactionBackend::runStatements;
    LOG(StorageAPI, "Scheduling runStatements for transaction %p\n", this);
    m_database->scheduleTransactionStep(this);
}

void SQLTransactionBackend::postflightAndCommit()
{
    ASSERT(m_lockAcquired);

    // Transaction Step 7 - Peform postflight steps, jumping to the error callback if they fail
    if (m_wrapper && !m_wrapper->performPostflight(SQLTransaction::from(this))) {
        m_transactionError = m_wrapper->sqlError();
        if (!m_transactionError) {
            m_database->reportCommitTransactionResult(3, SQLError::UNKNOWN_ERR, 0);
            m_transactionError = SQLError::create(SQLError::UNKNOWN_ERR, "unknown error occurred during transaction postflight");
        }
        handleTransactionError(false);
        return;
    }

    // Transacton Step 8+9 - Commit the transaction, jumping to the error callback if that fails
    ASSERT(m_sqliteTransaction);

    m_database->disableAuthorizer();
    m_sqliteTransaction->commit();
    m_database->enableAuthorizer();

    // If the commit failed, the transaction will still be marked as "in progress"
    if (m_sqliteTransaction->inProgress()) {
        if (m_wrapper)
            m_wrapper->handleCommitFailedAfterPostflight(SQLTransaction::from(this));
        m_successCallbackWrapper.clear();
        m_database->reportCommitTransactionResult(4, SQLError::DATABASE_ERR, m_database->sqliteDatabase().lastError());
        m_transactionError = SQLError::create(SQLError::DATABASE_ERR, "unable to commit transaction",
            m_database->sqliteDatabase().lastError(), m_database->sqliteDatabase().lastErrorMsg());
        handleTransactionError(false);
        return;
    }

    m_database->reportCommitTransactionResult(0, -1, 0); // OK

    // Vacuum the database if anything was deleted.
    if (m_database->hadDeletes())
        m_database->incrementalVacuumIfNeeded();

    // The commit was successful. If the transaction modified this database, notify the delegates.
    if (m_modifiedDatabase)
        m_database->transactionClient()->didCommitWriteTransaction(database());

    // Now release our unneeded callbacks, to break reference cycles.
    m_errorCallbackWrapper.clear();

    // Transaction Step 10 - Deliver success callback, if there is one
    if (m_successCallbackWrapper.hasCallback()) {
        m_nextStep = &SQLTransactionBackend::deliverSuccessCallback;
        LOG(StorageAPI, "Scheduling deliverSuccessCallback for transaction %p\n", this);
        m_database->scheduleTransactionCallback(SQLTransaction::from(this));
    } else
        cleanupAfterSuccessCallback();
}

void SQLTransactionBackend::deliverSuccessCallback()
{
    // Transaction Step 10 - Deliver success callback
    RefPtr<VoidCallback> successCallback = m_successCallbackWrapper.unwrap();
    if (successCallback)
        successCallback->handleEvent();

    // Schedule a "post-success callback" step to return control to the database thread in case there
    // are further transactions queued up for this Database
    m_nextStep = &SQLTransactionBackend::cleanupAfterSuccessCallback;
    LOG(StorageAPI, "Scheduling cleanupAfterSuccessCallback for transaction %p\n", this);
    m_database->scheduleTransactionStep(this);
}

void SQLTransactionBackend::cleanupAfterSuccessCallback()
{
    ASSERT(m_lockAcquired);

    // Transaction Step 11 - End transaction steps
    // There is no next step
    LOG(StorageAPI, "Transaction %p is complete\n", this);
    ASSERT(!m_database->sqliteDatabase().transactionInProgress());
    m_sqliteTransaction.clear();
    m_nextStep = 0;

    // Release the lock on this database
    m_database->transactionCoordinator()->releaseLock(this);
}

void SQLTransactionBackend::handleTransactionError(bool inCallback)
{
    if (m_errorCallbackWrapper.hasCallback()) {
        if (inCallback)
            deliverTransactionErrorCallback();
        else {
            m_nextStep = &SQLTransactionBackend::deliverTransactionErrorCallback;
            LOG(StorageAPI, "Scheduling deliverTransactionErrorCallback for transaction %p\n", this);
            m_database->scheduleTransactionCallback(SQLTransaction::from(this));
        }
        return;
    }

    // No error callback, so fast-forward to:
    // Transaction Step 12 - Rollback the transaction.
    if (inCallback) {
        m_nextStep = &SQLTransactionBackend::cleanupAfterTransactionErrorCallback;
        LOG(StorageAPI, "Scheduling cleanupAfterTransactionErrorCallback for transaction %p\n", this);
        m_database->scheduleTransactionStep(this);
    } else
        cleanupAfterTransactionErrorCallback();
}

void SQLTransactionBackend::deliverTransactionErrorCallback()
{
    ASSERT(m_transactionError);

    // Transaction Step 12 - If exists, invoke error callback with the last
    // error to have occurred in this transaction.
    RefPtr<SQLTransactionErrorCallback> errorCallback = m_errorCallbackWrapper.unwrap();
    if (errorCallback)
        errorCallback->handleEvent(m_transactionError.get());

    m_nextStep = &SQLTransactionBackend::cleanupAfterTransactionErrorCallback;
    LOG(StorageAPI, "Scheduling cleanupAfterTransactionErrorCallback for transaction %p\n", this);
    m_database->scheduleTransactionStep(this);
}

void SQLTransactionBackend::cleanupAfterTransactionErrorCallback()
{
    ASSERT(m_lockAcquired);

    m_database->disableAuthorizer();
    if (m_sqliteTransaction) {
        // Transaction Step 12 - Rollback the transaction.
        m_sqliteTransaction->rollback();

        ASSERT(!m_database->sqliteDatabase().transactionInProgress());
        m_sqliteTransaction.clear();
    }
    m_database->enableAuthorizer();

    // Transaction Step 12 - Any still-pending statements in the transaction are discarded.
    {
        MutexLocker locker(m_statementMutex);
        m_statementQueue.clear();
    }

    // Transaction is complete! There is no next step
    LOG(StorageAPI, "Transaction %p is complete with an error\n", this);
    ASSERT(!m_database->sqliteDatabase().transactionInProgress());
    m_nextStep = 0;

    // Now release the lock on this database
    m_database->transactionCoordinator()->releaseLock(this);
}

} // namespace WebCore

#endif // ENABLE(SQL_DATABASE)
