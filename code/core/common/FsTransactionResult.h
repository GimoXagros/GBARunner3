#pragma once

// Transport results are not FatFs FRESULT values. Only SUCCESS and IO_ERROR
// are published by ARM7; the remaining states describe ARM9 ownership.
typedef enum
{
    FS_RESULT_NO_TRANSACTION = 0,
    FS_RESULT_PENDING,
    FS_RESULT_SUCCESS,
    FS_RESULT_IO_ERROR,
    FS_RESULT_CANCELED,
    FS_RESULT_STALE_COMPLETION,
    FS_RESULT_TOKEN_MISMATCH,
    FS_RESULT_INVALID_ARGUMENT
} FsTransactionResult;
