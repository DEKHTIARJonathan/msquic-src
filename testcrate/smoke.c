/* MIT licensed packaging smoke tests. No upstream test suite is required. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "msquic.h"
#ifdef _WIN32
#include <wincrypt.h>
typedef CRITICAL_SECTION smoke_mutex;
typedef CONDITION_VARIABLE smoke_condition;
#define mutex_init(m) InitializeCriticalSection(m)
#define mutex_lock(m) EnterCriticalSection(m)
#define mutex_unlock(m) LeaveCriticalSection(m)
#define mutex_destroy(m) DeleteCriticalSection(m)
#define condition_init(c) InitializeConditionVariable(c)
#define condition_wake(c) WakeAllConditionVariable(c)
#define condition_destroy(c) ((void)(c))
#else
#include <pthread.h>
typedef pthread_mutex_t smoke_mutex;
typedef pthread_cond_t smoke_condition;
#define mutex_init(m) pthread_mutex_init(m, NULL)
#define mutex_lock(m) pthread_mutex_lock(m)
#define mutex_unlock(m) pthread_mutex_unlock(m)
#define mutex_destroy(m) pthread_mutex_destroy(m)
#define condition_init(c) pthread_cond_init(c, NULL)
#define condition_wake(c) pthread_cond_broadcast(c)
#define condition_destroy(c) pthread_cond_destroy(c)
#endif

static void check_status(QUIC_STATUS status, const char* operation) {
    if (QUIC_FAILED(status)) {
        fprintf(stderr, "%s failed: 0x%x\n", operation, status);
        abort();
    }
}

int smoke_api(void) {
    const QUIC_API_TABLE* api = NULL;
    check_status(MsQuicOpen2(&api), "MsQuicOpen2");
    uint32_t version[4] = {0};
    uint32_t length = sizeof(version);
    check_status(api->GetParam(NULL, QUIC_PARAM_GLOBAL_LIBRARY_VERSION, &length, version), "GetParam(version)");
    fprintf(stderr, "MsQuic native version: %u.%u.%u.%u\n", version[0], version[1], version[2], version[3]);
    HQUIC registration = NULL;
    QUIC_REGISTRATION_CONFIG config = {"msquic-src-api", QUIC_EXECUTION_PROFILE_LOW_LATENCY};
    check_status(api->RegistrationOpen(&config, &registration), "RegistrationOpen");
    api->RegistrationClose(registration);
    MsQuicClose(api);
    return length == sizeof(version) && version[0] == EXPECTED_MAJOR &&
        version[1] == EXPECTED_MINOR && version[2] == EXPECTED_PATCH ? 0 : 1;
}

typedef struct TEST_STATE TEST_STATE;
typedef struct STREAM_STATE {
    TEST_STATE* test;
    int server;
    size_t received;
    int replied;
} STREAM_STATE;

struct TEST_STATE {
    const QUIC_API_TABLE* api;
    HQUIC registration;
    HQUIC server_config;
    HQUIC client_config;
    HQUIC listener;
    HQUIC client;
    smoke_mutex mutex;
    smoke_condition condition;
    int connected;
    int active_connections;
    int received_reply;
    int stopping;
    int failed;
    STREAM_STATE server_stream;
    STREAM_STATE client_stream;
};

static uint8_t payload[] = "msquic-src packaging smoke test";
static QUIC_BUFFER send_buffer = {sizeof(payload) - 1, payload};

static void failure(TEST_STATE* test, const char* message) {
    fprintf(stderr, "loopback failure: %s\n", message);
    mutex_lock(&test->mutex);
    test->failed = 1;
    condition_wake(&test->condition);
    mutex_unlock(&test->mutex);
}

static QUIC_STATUS QUIC_API stream_callback(HQUIC stream, void* context, QUIC_STREAM_EVENT* event) {
    STREAM_STATE* state = context;
    TEST_STATE* test = state->test;
    if (event->Type == QUIC_STREAM_EVENT_RECEIVE) {
        int reply = 0;
        mutex_lock(&test->mutex);
        for (uint32_t i = 0; i < event->RECEIVE.BufferCount; i++) {
            const QUIC_BUFFER* buffer = &event->RECEIVE.Buffers[i];
            if (state->received + buffer->Length > sizeof(payload) - 1 ||
                memcmp(payload + state->received, buffer->Buffer, buffer->Length) != 0) {
                test->failed = 1;
                break;
            }
            state->received += buffer->Length;
        }
        if (!test->failed && state->received == sizeof(payload) - 1) {
            if (state->server && !state->replied) {
                state->replied = 1;
                reply = 1;
            } else if (!state->server) {
                test->received_reply = 1;
            }
        }
        condition_wake(&test->condition);
        mutex_unlock(&test->mutex);
        if (reply) {
            check_status(test->api->StreamSend(stream, &send_buffer, 1, QUIC_SEND_FLAG_FIN, NULL), "StreamSend(reply)");
        }
    } else if (event->Type == QUIC_STREAM_EVENT_START_COMPLETE && QUIC_FAILED(event->START_COMPLETE.Status)) {
        failure(test, "stream start");
    } else if (event->Type == QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE) {
        test->api->StreamClose(stream);
    }
    return QUIC_STATUS_SUCCESS;
}

static QUIC_STATUS QUIC_API connection_callback(HQUIC connection, void* context, QUIC_CONNECTION_EVENT* event) {
    TEST_STATE* test = context;
    if (event->Type == QUIC_CONNECTION_EVENT_CONNECTED) {
        mutex_lock(&test->mutex);
        test->connected++;
        condition_wake(&test->condition);
        mutex_unlock(&test->mutex);
    } else if (event->Type == QUIC_CONNECTION_EVENT_PEER_STREAM_STARTED) {
        test->api->SetCallbackHandler(event->PEER_STREAM_STARTED.Stream, (void*)stream_callback, &test->server_stream);
    } else if (event->Type == QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_TRANSPORT) {
        mutex_lock(&test->mutex);
        if (!test->stopping) {
            fprintf(stderr, "transport shutdown: 0x%x\n", event->SHUTDOWN_INITIATED_BY_TRANSPORT.Status);
            test->failed = 1;
        }
        condition_wake(&test->condition);
        mutex_unlock(&test->mutex);
    } else if (event->Type == QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE) {
        test->api->ConnectionClose(connection);
        mutex_lock(&test->mutex);
        test->active_connections--;
        condition_wake(&test->condition);
        mutex_unlock(&test->mutex);
    }
    return QUIC_STATUS_SUCCESS;
}

static QUIC_STATUS QUIC_API listener_callback(HQUIC listener, void* context, QUIC_LISTENER_EVENT* event) {
    (void)listener;
    TEST_STATE* test = context;
    if (event->Type == QUIC_LISTENER_EVENT_NEW_CONNECTION) {
        HQUIC connection = event->NEW_CONNECTION.Connection;
        mutex_lock(&test->mutex);
        test->active_connections++;
        mutex_unlock(&test->mutex);
        test->api->SetCallbackHandler(connection, (void*)connection_callback, test);
        check_status(test->api->ConnectionSetConfiguration(connection, test->server_config), "ConnectionSetConfiguration");
    }
    return QUIC_STATUS_SUCCESS;
}

static void wait_for(TEST_STATE* test, int stage) {
#ifdef _WIN32
    ULONGLONG deadline = GetTickCount64() + 20000;
#else
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += 20;
#endif
    mutex_lock(&test->mutex);
    while (!test->failed) {
        if ((stage == 0 && test->connected == 2) ||
            (stage == 1 && test->received_reply) ||
            (stage == 2 && test->active_connections == 0)) {
            mutex_unlock(&test->mutex);
            return;
        }
#ifdef _WIN32
        ULONGLONG now = GetTickCount64();
        if (now >= deadline || !SleepConditionVariableCS(&test->condition, &test->mutex, (DWORD)(deadline - now))) break;
#else
        if (pthread_cond_timedwait(&test->condition, &test->mutex, &deadline) != 0) break;
#endif
    }
    fprintf(stderr, "loopback stage %d failed/timed out (connected=%d, reply=%d, active=%d)\n",
        stage, test->connected, test->received_reply, test->active_connections);
    mutex_unlock(&test->mutex);
    abort();
}

int smoke_loopback(const char* certificate, const char* key, const uint8_t* pfx, uint32_t pfx_length) {
    TEST_STATE test = {0};
    mutex_init(&test.mutex);
    condition_init(&test.condition);
    test.server_stream.test = &test;
    test.server_stream.server = 1;
    test.client_stream.test = &test;
    check_status(MsQuicOpen2(&test.api), "MsQuicOpen2");
    QUIC_REGISTRATION_CONFIG registration = {"msquic-src-loopback", QUIC_EXECUTION_PROFILE_LOW_LATENCY};
    check_status(test.api->RegistrationOpen(&registration, &test.registration), "RegistrationOpen");
    uint8_t alpn_text[] = "msquic-src-smoke";
    QUIC_BUFFER alpn = {sizeof(alpn_text) - 1, alpn_text};
    QUIC_SETTINGS settings = {0};
    settings.IsSet.PeerBidiStreamCount = 1;
    settings.PeerBidiStreamCount = 1;
    settings.IsSet.IdleTimeoutMs = 1;
    settings.IdleTimeoutMs = 15000;
    check_status(test.api->ConfigurationOpen(test.registration, &alpn, 1, &settings, sizeof(settings), NULL, &test.server_config), "ConfigurationOpen(server)");
    check_status(test.api->ConfigurationOpen(test.registration, &alpn, 1, &settings, sizeof(settings), NULL, &test.client_config), "ConfigurationOpen(client)");
    QUIC_CERTIFICATE_FILE files = {key, certificate};
    QUIC_CREDENTIAL_CONFIG server_credential = {0};
    server_credential.Type = QUIC_CREDENTIAL_TYPE_CERTIFICATE_FILE;
    server_credential.CertificateFile = &files;
#ifdef _WIN32
    QUIC_TLS_PROVIDER provider;
    uint32_t provider_length = sizeof(provider);
    check_status(test.api->GetParam(NULL, QUIC_PARAM_GLOBAL_TLS_PROVIDER, &provider_length, &provider), "GetParam(TLS provider)");
    HCERTSTORE store = NULL;
    PCCERT_CONTEXT context = NULL;
    if (provider == QUIC_TLS_PROVIDER_SCHANNEL) {
        CRYPT_DATA_BLOB blob = {pfx_length, (BYTE*)pfx};
        store = PFXImportCertStore(&blob, L"", CRYPT_USER_KEYSET);
        if (!store) abort();
        context = CertEnumCertificatesInStore(store, NULL);
        if (!context) abort();
        server_credential.Type = QUIC_CREDENTIAL_TYPE_CERTIFICATE_CONTEXT;
        server_credential.CertificateContext = (QUIC_CERTIFICATE*)context;
    }
#else
    (void)pfx;
    (void)pfx_length;
#endif
    check_status(test.api->ConfigurationLoadCredential(test.server_config, &server_credential), "ConfigurationLoadCredential(server)");
#ifdef _WIN32
    if (context) CertFreeCertificateContext(context);
    if (store) CertCloseStore(store, 0);
#endif
    QUIC_CREDENTIAL_CONFIG client_credential = {0};
    client_credential.Type = QUIC_CREDENTIAL_TYPE_NONE;
    /* Local test fixture only; no dependency on the host's trust store. */
    client_credential.Flags = QUIC_CREDENTIAL_FLAG_CLIENT | QUIC_CREDENTIAL_FLAG_NO_CERTIFICATE_VALIDATION;
    check_status(test.api->ConfigurationLoadCredential(test.client_config, &client_credential), "ConfigurationLoadCredential(client)");
    check_status(test.api->ListenerOpen(test.registration, listener_callback, &test, &test.listener), "ListenerOpen");
    QUIC_ADDR address = {0};
    QuicAddrSetFamily(&address, QUIC_ADDRESS_FAMILY_INET);
    QuicAddrSetToLoopback(&address);
    check_status(test.api->ListenerStart(test.listener, &alpn, 1, &address), "ListenerStart");
    uint32_t length = sizeof(address);
    check_status(test.api->GetParam(test.listener, QUIC_PARAM_LISTENER_LOCAL_ADDRESS, &length, &address), "GetParam(listener address)");
    check_status(test.api->ConnectionOpen(test.registration, connection_callback, &test, &test.client), "ConnectionOpen");
    mutex_lock(&test.mutex);
    test.active_connections++;
    mutex_unlock(&test.mutex);
    check_status(test.api->ConnectionStart(test.client, test.client_config, QUIC_ADDRESS_FAMILY_INET, "127.0.0.1", QuicAddrGetPort(&address)), "ConnectionStart");
    wait_for(&test, 0);
    HQUIC stream = NULL;
    check_status(test.api->StreamOpen(test.client, QUIC_STREAM_OPEN_FLAG_NONE, stream_callback, &test.client_stream, &stream), "StreamOpen");
    check_status(test.api->StreamStart(stream, QUIC_STREAM_START_FLAG_IMMEDIATE), "StreamStart");
    check_status(test.api->StreamSend(stream, &send_buffer, 1, QUIC_SEND_FLAG_FIN, NULL), "StreamSend");
    wait_for(&test, 1);
    mutex_lock(&test.mutex);
    test.stopping = 1;
    mutex_unlock(&test.mutex);
    test.api->ListenerClose(test.listener);
    test.api->RegistrationShutdown(test.registration, QUIC_CONNECTION_SHUTDOWN_FLAG_NONE, 0);
    wait_for(&test, 2);
    test.api->ConfigurationClose(test.server_config);
    test.api->ConfigurationClose(test.client_config);
    test.api->RegistrationClose(test.registration);
    MsQuicClose(test.api);
    condition_destroy(&test.condition);
    mutex_destroy(&test.mutex);
    return 0;
}
