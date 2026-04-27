#ifndef FOTA_MANAGER_H
#define FOTA_MANAGER_H

/**
 * @file  fota_manager.h
 * @brief Firmware-Over-The-Air (FOTA) manager for bare-metal IoT devices.
 *
 * Implements the State design pattern in pure C:
 *   - Each FSM state owns an on_enter / on_event / on_execute vtable.
 *   - A central transition engine (do_transition) drives the table and
 *     fires the registered callbacks (state_change, error, execute).
 *   - A circular event queue decouples producers from the super-loop.
 * No RTOS required. Pure C99.
 *
 * Happy-path workflow:
 *
 *   ST_APP --> [EV_CHECK] --> ST_CHECK
 *          --> [EV_FOUND] --> ST_DOWNLOAD
 *          --> [EV_DOWNLOADED] --> ST_VERIFY
 *          --> [EV_VERIFIED] --> ST_APPLY
 *          --> [EV_APPLIED] --> ST_REBOOT
 *          --> [EV_REBOOTED] --> ST_APP
 *
 * Failure paths:
 *
 *   Any active state --> [EV_FAILED | EV_TIMEOUT] --> ST_ERROR
 *   ST_ERROR --> [EV_ROLLBACK] --> ST_ROLLBACK
 *   ST_ROLLBACK --> [EV_APPLIED] --> ST_REBOOT
 *   Any state --> [EV_RESET] --> ST_APP  (immediate, no queue)
 *
 * Retry path:
 *
 *   Any active state --> [EV_RETRY] --> same state, retry_count++
 *   When retry_count >= FOTA_MAX_RETRIES --> ST_ERROR
 *
 * Typical usage:
 *
 *   @code
 *   fota_set_current_version(&my_version);
 *   fota_set_state_change_cb(my_state_hook);
 *   fota_set_execute_cb(my_platform_hook);
 *   fota_init();
 *
 *   // Super-loop
 *   while (1) {
 *       fota_enqueue(EV_CHECK);   // driven by timer / network event
 *       fota_process();           // call every tick
 *   }
 *   @endcode
 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ===================== Configuration ===================== */

/**
 * @defgroup fota_config Compile-time configuration
 * @{
 */

/** Size of the circular event queue. Must be a power of two. */
#ifndef FOTA_QSIZE
#define FOTA_QSIZE          16
#endif

/** Maximum automatic retries before the FSM enters ST_ERROR. */
#ifndef FOTA_MAX_RETRIES
#define FOTA_MAX_RETRIES    3
#endif

/**
 * Maximum length of a firmware version label string,
 * including the null terminator.
 */
#ifndef FOTA_VERSION_LEN
#define FOTA_VERSION_LEN    32
#endif

/**
 * Maximum length of a firmware download URL string,
 * including the null terminator.
 */
#ifndef FOTA_URL_LEN
#define FOTA_URL_LEN        128
#endif

/**
 * Maximum length of a SHA-256 hex digest string
 * (64 hex chars + null terminator = 65).
 */
#ifndef FOTA_HASH_LEN
#define FOTA_HASH_LEN       65
#endif

/** @} */

/* ===================== Version Comparison Macros ===================== */

/**
 * @defgroup fota_version_macros Version helper macros
 * @{
 */

/**
 * @brief True when two FotaVersion structs have identical major.minor.patch.
 *
 * Label strings are intentionally ignored so that "1.2.3-rc1" and
 * "1.2.3-release" compare as equal.
 */
#define FOTA_VERSION_EQUALS(a, b)  \
    ((a).major == (b).major &&     \
     (a).minor == (b).minor &&     \
     (a).patch == (b).patch)

/**
 * @brief True when version @p a is strictly greater than version @p b.
 *
 * Comparison order: major > minor > patch.
 */
#define FOTA_VERSION_GREATER(a, b)                          \
    ( (a).major > (b).major ||                              \
      ((a).major == (b).major && (a).minor > (b).minor) ||  \
      ((a).major == (b).major && (a).minor == (b).minor &&  \
       (a).patch > (b).patch) )

/** @} */

/* ===================== Error Codes ===================== */

/**
 * @brief Return codes used throughout the FOTA manager API.
 *
 * All functions that can fail return a FotaError.
 * FOTA_OK (0) always means success; negative values are errors.
 */
typedef enum {
    FOTA_OK             =  0,  /**< Operation completed successfully.            */
    FOTA_ERR_QUEUE_FULL = -1,  /**< Event queue is full; event was dropped.      */
    FOTA_ERR_TIMEOUT    = -2,  /**< Operation timed out.                         */
    FOTA_ERR_HASH       = -3,  /**< Firmware image hash verification failed.     */
    FOTA_ERR_FLASH      = -4,  /**< Flash write or erase operation failed.       */
    FOTA_ERR_ROLLBACK   = -5,  /**< Rollback to previous firmware failed.        */
    FOTA_ERR_NO_UPDATE  = -6,  /**< No firmware update available on server.      */
    FOTA_ERR_BAD_STATE  = -7,  /**< Operation not allowed in the current state.  */
    FOTA_ERR_NULL_PTR   = -8,  /**< NULL pointer passed to a function.           */
    FOTA_ERR_OVERFLOW   = -9   /**< Internal buffer overflow detected.           */
} FotaError;

/* ===================== Events ===================== */

/**
 * @brief Events that drive FSM transitions.
 *
 * Enqueue events with fota_enqueue(). The FSM consumes one event
 * per fota_process() call, in FIFO order.
 */
typedef enum {
    EV_CHECK,       /**< Trigger a server-side update availability check.       */
    EV_FOUND,       /**< Server reported a newer firmware version is available. */
    EV_NOT_FOUND,   /**< Server confirmed firmware is already up-to-date.       */
    EV_DOWNLOADED,  /**< Image download completed successfully.                 */
    EV_VERIFIED,    /**< Image hash / signature check passed.                   */
    EV_APPLIED,     /**< Flash write of new image completed successfully.       */
    EV_REBOOTED,    /**< Device completed the post-flash reboot cycle.          */
    EV_FAILED,      /**< Generic failure in the current stage.                  */
    EV_TIMEOUT,     /**< Stage watchdog expired; treated as a failure.          */
    EV_ROLLBACK,    /**< Explicit request to restore the previous image.        */
    EV_RETRY,       /**< Retry the current stage after a transient fault.       */
    EV_RESET        /**< Force FSM back to ST_APP unconditionally.              */
} FotaEvent;

/* ===================== States ===================== */

/**
 * @brief FSM states.
 *
 * The manager moves between states in response to events.
 * Invalid event/state combinations are silently ignored so
 * the FSM is always in a well-defined state.
 */
typedef enum {
    ST_APP,         /**< Normal application operation (idle / not updating).    */
    ST_CHECK,       /**< Querying the update server for a newer version.        */
    ST_DOWNLOAD,    /**< Downloading the firmware image from the server.        */
    ST_VERIFY,      /**< Verifying image integrity (hash / signature check).    */
    ST_APPLY,       /**< Writing the new image to flash.                        */
    ST_REBOOT,      /**< Waiting for the device to reboot into new firmware.    */
    ST_ROLLBACK,    /**< Restoring the previous firmware image from backup.     */
    ST_ERROR        /**< Unrecoverable error; awaiting EV_ROLLBACK or EV_RESET. */
} FotaState;

/* ===================== Data Structures ===================== */

/**
 * @brief Semantic version of a firmware image.
 *
 * Follows Semantic Versioning 2.0.0 (semver.org).
 * Use FOTA_VERSION_EQUALS / FOTA_VERSION_GREATER for comparisons.
 */
typedef struct {
    uint16_t major;                   /**< Breaking changes.                    */
    uint16_t minor;                   /**< Backwards-compatible additions.      */
    uint16_t patch;                   /**< Backwards-compatible bug fixes.      */
    char     label[FOTA_VERSION_LEN]; /**< Optional pre-release label (e.g. "rc1"). */
} FotaVersion;

/**
 * @brief Metadata of a pending firmware update.
 *
 * Populated by the application after EV_FOUND is processed,
 * typically from the server manifest response.
 * Store it with fota_set_update_info().
 */
typedef struct {
    FotaVersion version;              /**< Version of the incoming image.       */
    char        url [FOTA_URL_LEN];   /**< Source URL for the image download.   */
    char        hash[FOTA_HASH_LEN];  /**< Expected SHA-256 hex digest.         */
    uint32_t    size;                 /**< Expected image size in bytes.        */
} FotaUpdateInfo;

/**
 * @brief Runtime status snapshot returned by fota_get_status().
 *
 * All fields are valid immediately after fota_get_status() returns.
 */
typedef struct {
    FotaState   state;                /**< Current FSM state.                   */
    FotaError   last_error;           /**< Last recorded error (FOTA_OK = none).*/
    uint8_t     retry_count;          /**< Retries attempted in current stage.  */
    uint32_t    events_processed;     /**< Total events consumed since init.    */
    FotaVersion current_version;      /**< Version of the running firmware.     */
    FotaVersion pending_version;      /**< Incoming firmware version (if known).*/
} FotaStatus;

/* ===================== Callback Types ===================== */

/**
 * @brief Called every time the FSM transitions to a new state.
 *
 * @param prev  State before the transition.
 * @param next  State after the transition.
 */
typedef void (*FotaStateChangeCb)(FotaState prev, FotaState next);

/**
 * @brief Called when the FSM enters ST_ERROR.
 *
 * @param error  The error code that triggered the transition.
 */
typedef void (*FotaErrorCb)(FotaError error);

/**
 * @brief Called on each fota_execute() tick for the active state.
 *
 * Use this hook to bind platform-specific actions (HTTP requests,
 * flash writes, watchdog kicks) to each state without modifying
 * the manager source.
 *
 * @param state  The state currently being executed.
 */
typedef void (*FotaExecuteCb)(FotaState state);

/* ===================== Public API ===================== */

/**
 * @defgroup fota_api Public API
 * @{
 */

/**
 * @brief Initialise (or re-initialise) the FOTA manager.
 *
 * Resets the event queue, FSM state to ST_APP, retry counter,
 * and all status fields. Must be called once before any other
 * FOTA function.
 *
 * @note Callbacks registered before fota_init() are preserved so
 *       the manager can be re-initialised without losing hooks.
 */
void fota_init(void);

/**
 * @brief Push an event onto the tail of the circular event queue.
 *
 * @note Not interrupt-safe. Disable interrupts or acquire a mutex
 *       before calling from an ISR context.
 *
 * @param event  The event to enqueue.
 * @return       FOTA_OK on success, FOTA_ERR_QUEUE_FULL if the
 *               queue has no free slots.
 */
FotaError fota_enqueue(FotaEvent event);

/**
 * @brief Consume one event and advance the FSM by one step.
 *
 * Call this from the super-loop at the desired cadence. Each call:
 *   1. Dequeues one event (no-op if the queue is empty).
 *   2. Calls the internal transition function (fota_update).
 *   3. Calls the internal execute function (fota_execute).
 */
void fota_process(void);

/**
 * @brief Force the FSM back to ST_APP and clear the retry counter.
 *
 * Takes effect immediately without waiting for fota_process().
 * Any pending events are left in the queue.
 */
void fota_reset(void);

/**
 * @brief Discard all pending events from the queue.
 *
 * Useful before a forced fota_reset() to prevent stale events
 * from triggering unexpected transitions after recovery.
 */
void fota_flush_queue(void);

/**
 * @brief Return the current FSM state.
 *
 * @return Current FotaState value.
 */
FotaState fota_get_state(void);

/**
 * @brief Check whether the FSM is idle (ST_APP).
 *
 * @return true if state == ST_APP, false otherwise.
 */
bool fota_is_idle(void);

/**
 * @brief Check whether the FSM is in an unrecoverable error state.
 *
 * @return true if state == ST_ERROR, false otherwise.
 */
bool fota_has_error(void);

/**
 * @brief Fill @p status with a snapshot of the current runtime status.
 *
 * @param status  Pointer to a caller-allocated FotaStatus struct.
 * @return        FOTA_OK, or FOTA_ERR_NULL_PTR if @p status is NULL.
 */
FotaError fota_get_status(FotaStatus *status);

/**
 * @brief Store the running firmware version.
 *
 * Call once during boot, before fota_init(), so that
 * fota_get_status() reports accurate version information.
 *
 * @param version  Pointer to a FotaVersion describing the live image.
 * @return         FOTA_OK, or FOTA_ERR_NULL_PTR if @p version is NULL.
 */
FotaError fota_set_current_version(const FotaVersion *version);

/**
 * @brief Store metadata for the pending firmware update.
 *
 * Typically called by the application after the server confirms a
 * newer version is available (after processing EV_FOUND).
 *
 * @param info  Pointer to a populated FotaUpdateInfo struct.
 * @return      FOTA_OK, or FOTA_ERR_NULL_PTR if @p info is NULL.
 */
FotaError fota_set_update_info(const FotaUpdateInfo *info);

/**
 * @brief Retrieve the stored pending update metadata.
 *
 * @param info  Pointer to a caller-allocated FotaUpdateInfo struct.
 * @return      FOTA_OK, or FOTA_ERR_NULL_PTR if @p info is NULL.
 */
FotaError fota_get_update_info(FotaUpdateInfo *info);

/**
 * @brief Register a callback invoked on every FSM state transition.
 *
 * Pass NULL to unregister a previously registered callback.
 *
 * @param cb  Callback function pointer, or NULL.
 */
void fota_set_state_change_cb(FotaStateChangeCb cb);

/**
 * @brief Register a callback invoked when the FSM enters ST_ERROR.
 *
 * Pass NULL to unregister a previously registered callback.
 *
 * @param cb  Callback function pointer, or NULL.
 */
void fota_set_error_cb(FotaErrorCb cb);

/**
 * @brief Register a callback invoked on every fota_execute() tick.
 *
 * Use this to bind platform actions (HTTP, flash, watchdog) to
 * each state without modifying fota_manager.c.
 * Pass NULL to unregister a previously registered callback.
 *
 * @param cb  Callback function pointer, or NULL.
 */
void fota_set_execute_cb(FotaExecuteCb cb);

/**
 * @brief Convert a FotaState value to a human-readable C string.
 *
 * The returned pointer is always valid (never NULL) and points to
 * a static string; do not free it.
 *
 * @param state  FSM state value.
 * @return       Null-terminated ASCII string, e.g. "ST_DOWNLOAD".
 */
const char *fota_state_to_str(FotaState state);

/**
 * @brief Convert a FotaEvent value to a human-readable C string.
 *
 * @param event  FSM event value.
 * @return       Null-terminated ASCII string, e.g. "EV_VERIFIED".
 */
const char *fota_event_to_str(FotaEvent event);

/**
 * @brief Convert a FotaError value to a human-readable C string.
 *
 * @param error  Error code.
 * @return       Null-terminated ASCII string, e.g. "FOTA_ERR_HASH".
 */
const char *fota_error_to_str(FotaError error);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* FOTA_MANAGER_H */
