// HTTP Tests — requires test server: go run test/test_server.go

// ============================================================================
// HTTP Tests — default HTTPS/h2 on port 8788 (requires test server)
// ============================================================================

#define TEST_URL "https://127.0.0.1:8788"
#define TEST_URL_HTTP "http://127.0.0.1:8787"

// All tests use HTTPS by default (HTTP/2 via ALPN). Skip cert verification
// for the self-signed test cert.
WebRequest NewRequest(any userdata = 0) {
    WebRequest req = async2_HttpNew(userdata);
    req.SetOptInt(CURLOPT_SSL_VERIFYPEER, 0);
    req.SetOptInt(CURLOPT_SSL_VERIFYHOST, 0);
    return req;
}

void Test_HTTP_Get() {
    g_http_pending++;
    WebRequest req = NewRequest();
    req.Execute("GET", TEST_URL ... "/json", OnHttpGet);
}

public void OnHttpGet(WebRequest req, int curlcode, int httpcode, int size) {
    g_http_pending--;
    AssertEq(curlcode, 0, "HTTP GET curlcode");
    AssertEq(httpcode, 200, "HTTP GET httpcode");
    Assert(size > 0, "HTTP GET has body");

    Json json = Json.ParseResponse(req);
    Assert(view_as<int>(json) != 0, "HTTP GET parse JSON");

    char name[64];
    json.GetString("name", name, sizeof(name));
    AssertStrEq(name, "async2", "HTTP GET json.name");
    AssertEq(json.GetInt("version"), 1, "HTTP GET json.version");
    Assert(json.GetBool("active"), "HTTP GET json.active");

    Json tags = json.GetArray("tags");
    AssertEq(tags.ArrayLength, 2, "HTTP GET tags length");
    tags.Close();

    json.Close();

    MaybeFinishAll();
}

void Test_HTTP_PostJson() {
    g_http_pending++;
    Json body = Json.CreateObject();
    body.SetString("msg", "hello");
    body.SetInt("num", 42);

    WebRequest req = NewRequest();
    req.SetBodyJSON(body);
    req.Execute("POST", TEST_URL ... "/post", OnHttpPostJson);
    body.Close();
}

public void OnHttpPostJson(WebRequest req, int curlcode, int httpcode, int size) {
    g_http_pending--;
    AssertEq(curlcode, 0, "HTTP POST JSON curlcode");
    AssertEq(httpcode, 200, "HTTP POST JSON httpcode");

    Json json = Json.ParseResponse(req);
    Assert(view_as<int>(json) != 0, "HTTP POST JSON parse");

    Json data = json.GetObject("data");
    Assert(view_as<int>(data) != 0, "HTTP POST JSON data obj");

    char msg[64];
    data.GetString("msg", msg, sizeof(msg));
    AssertStrEq(msg, "hello", "HTTP POST JSON data.msg");
    AssertEq(data.GetInt("num"), 42, "HTTP POST JSON data.num");

    // Verify Content-Type was set by SetBodyJSON
    Json headers = json.GetObject("headers");
    char ct[128];
    headers.GetString("Content-Type", ct, sizeof(ct));
    AssertStrEq(ct, "application/json", "HTTP POST JSON content-type");
    headers.Close();

    data.Close();
    json.Close();

    MaybeFinishAll();
}

void Test_HTTP_PostString() {
    g_http_pending++;
    WebRequest req = NewRequest();
    req.SetBodyString("raw body text");
    req.Execute("POST", TEST_URL ... "/echo", OnHttpPostString);
}

public void OnHttpPostString(WebRequest req, int curlcode, int httpcode, int size) {
    g_http_pending--;
    AssertEq(curlcode, 0, "HTTP POST string curlcode");
    AssertEq(httpcode, 200, "HTTP POST string httpcode");

    char buf[256];
    req.GetString(buf, sizeof(buf));
    AssertStrEq(buf, "raw body text", "HTTP POST string echo body");


    MaybeFinishAll();
}

void Test_HTTP_CustomHeader() {
    g_http_pending++;
    WebRequest req = NewRequest();
    req.SetHeader("X-Test-Header", "test_value");
    req.Execute("GET", TEST_URL ... "/headers", OnHttpCustomHeader);
}

public void OnHttpCustomHeader(WebRequest req, int curlcode, int httpcode, int size) {
    g_http_pending--;
    AssertEq(curlcode, 0, "HTTP header curlcode");

    Json json = Json.ParseResponse(req);
    Json headers = json.GetObject("headers");

    char val[128];
    headers.GetString("X-Test-Header", val, sizeof(val));
    AssertStrEq(val, "test_value", "HTTP custom header echoed");

    headers.Close();
    json.Close();

    MaybeFinishAll();
}

void Test_HTTP_Status404() {
    g_http_pending++;
    WebRequest req = NewRequest();
    req.Execute("GET", TEST_URL ... "/status/404", OnHttpStatus404);
}

public void OnHttpStatus404(WebRequest req, int curlcode, int httpcode, int size) {
    g_http_pending--;
    AssertEq(curlcode, 0, "HTTP 404 curlcode");
    AssertEq(httpcode, 404, "HTTP 404 httpcode");

    MaybeFinishAll();
}

void Test_HTTP_Delete() {
    g_http_pending++;
    WebRequest req = NewRequest();
    req.Execute("DELETE", TEST_URL ... "/resource", OnHttpDelete);
}

public void OnHttpDelete(WebRequest req, int curlcode, int httpcode, int size) {
    g_http_pending--;
    AssertEq(curlcode, 0, "HTTP DELETE curlcode");
    AssertEq(httpcode, 200, "HTTP DELETE httpcode");

    Json json = Json.ParseResponse(req);
    Assert(json.GetBool("deleted"), "HTTP DELETE json.deleted");
    json.Close();

    MaybeFinishAll();
}

void Test_HTTP_EmptyBody() {
    g_http_pending++;
    WebRequest req = NewRequest();
    req.Execute("GET", TEST_URL ... "/empty", OnHttpEmptyBody);
}

public void OnHttpEmptyBody(WebRequest req, int curlcode, int httpcode, int size) {
    g_http_pending--;
    AssertEq(curlcode, 0, "HTTP empty curlcode");
    AssertEq(httpcode, 200, "HTTP empty httpcode");
    AssertEq(size, 0, "HTTP empty body size");

    MaybeFinishAll();
}

void Test_HTTP_LargeResponse() {
    g_http_pending++;
    WebRequest req = NewRequest();
    req.Execute("GET", TEST_URL ... "/large", OnHttpLargeResponse);
}

public void OnHttpLargeResponse(WebRequest req, int curlcode, int httpcode, int size) {
    g_http_pending--;
    AssertEq(curlcode, 0, "HTTP large curlcode");
    AssertEq(httpcode, 200, "HTTP large httpcode");
    Assert(size > 0, "HTTP large has body");

    Json json = Json.ParseResponse(req);
    Assert(view_as<int>(json) != 0, "HTTP large parse JSON");
    AssertEq(json.GetInt("key_0"), 0, "HTTP large key_0");
    AssertEq(json.GetInt("key_99"), 99, "HTTP large key_99");
    json.Close();

    MaybeFinishAll();
}

void Test_HTTP_Redirect() {
    g_http_pending++;
    WebRequest req = NewRequest();
    req.Execute("GET", TEST_URL ... "/redirect", OnHttpRedirect);
}

public void OnHttpRedirect(WebRequest req, int curlcode, int httpcode, int size) {
    g_http_pending--;
    AssertEq(curlcode, 0, "HTTP redirect curlcode");
    AssertEq(httpcode, 200, "HTTP redirect followed to 200");

    MaybeFinishAll();
}

void Test_HTTP_ConnRefused() {
    g_http_pending++;
    WebRequest req = NewRequest();
    req.SetOptInt(CURLOPT_CONNECTTIMEOUT_MS, 1000);
    req.Execute("GET", "http://127.0.0.1:1/nope", OnHttpConnRefused);
}

public void OnHttpConnRefused(WebRequest req, int curlcode, int httpcode, int size) {
    g_http_pending--;
    Assert(curlcode != 0, "HTTP conn refused curlcode non-zero");

    MaybeFinishAll();
}

void Test_HTTP_ResponseHeader() {
    g_http_pending++;
    WebRequest req = NewRequest();
    req.Execute("GET", TEST_URL ... "/json", OnHttpResponseHeader);
}

public void OnHttpResponseHeader(WebRequest req, int curlcode, int httpcode, int size) {
    g_http_pending--;
    AssertEq(curlcode, 0, "HTTP resp header curlcode");

    char ct[128];
    int hsize = req.GetResponseHeader("Content-Type", ct, sizeof(ct));
    Assert(hsize > 0, "HTTP resp header Content-Type found");
    Assert(StrContains(ct, "application/json") != -1, "HTTP resp header Content-Type is json");

    int missing = req.GetResponseHeaderLength("X-Nonexistent");
    AssertEq(missing, -1, "HTTP resp header missing returns -1");


    MaybeFinishAll();
}

void Test_HTTP_Gzip() {
    g_http_pending++;
    WebRequest req = NewRequest();
    req.Execute("GET", TEST_URL ... "/gzip", OnHttpGzip);
}

public void OnHttpGzip(WebRequest req, int curlcode, int httpcode, int size) {
    g_http_pending--;
    AssertEq(curlcode, 0, "HTTP gzip curlcode");
    AssertEq(httpcode, 200, "HTTP gzip httpcode");

    Json json = Json.ParseResponse(req);
    Assert(view_as<int>(json) != 0, "HTTP gzip parse JSON");
    Assert(json.GetBool("compressed"), "HTTP gzip json.compressed");

    char enc[32];
    json.GetString("encoding", enc, sizeof(enc));
    AssertStrEq(enc, "gzip", "HTTP gzip json.encoding");

    json.Close();

    MaybeFinishAll();
}

void Test_HTTP_Deflate_Upload() {
    g_http_pending++;
    // Body must be large enough that deflate actually shrinks it
    char body[512];
    for (int i = 0; i < sizeof(body) - 1; i++)
        body[i] = 'A';
    body[sizeof(body) - 1] = '\0';

    WebRequest req = NewRequest();
    req.SetBodyString(body);
    req.SetCompression(true);
    req.Execute("POST", TEST_URL ... "/deflate", OnHttpDeflateUpload);
}

public void OnHttpDeflateUpload(WebRequest req, int curlcode, int httpcode, int size) {
    g_http_pending--;
    AssertEq(curlcode, 0, "HTTP deflate upload curlcode");
    AssertEq(httpcode, 200, "HTTP deflate upload httpcode");

    Json json = Json.ParseResponse(req);
    Assert(view_as<int>(json) != 0, "HTTP deflate upload parse");

    int decompressed_size = json.GetInt("decompressed_size");
    AssertEq(decompressed_size, 511, "HTTP deflate upload decompressed size");

    // Compressed should be smaller than original
    int compressed_size = json.GetInt("compressed_size");
    Assert(compressed_size < decompressed_size, "HTTP deflate upload actually compressed");

    json.Close();

    MaybeFinishAll();
}

void Test_HTTP_PlainHTTP() {
    // Verify plain HTTP/1.1 still works (all other tests use HTTPS/h2)
    g_http_pending++;
    WebRequest req = async2_HttpNew();
    req.Execute("GET", TEST_URL_HTTP ... "/json", OnHttpPlainHTTP);
}

public void OnHttpPlainHTTP(WebRequest req, int curlcode, int httpcode, int size) {
    g_http_pending--;
    AssertEq(curlcode, 0, "HTTP/1.1 curlcode");
    AssertEq(httpcode, 200, "HTTP/1.1 httpcode");
    Assert(size > 0, "HTTP/1.1 has body");

    Json json = Json.ParseResponse(req);
    Assert(view_as<int>(json) != 0, "HTTP/1.1 parse JSON");

    char name[64];
    json.GetString("name", name, sizeof(name));
    AssertStrEq(name, "async2", "HTTP/1.1 json.name");

    json.Close();

    MaybeFinishAll();
}

void Test_HTTP_Put() {
    g_http_pending++;
    WebRequest req = NewRequest();
    req.SetBodyString("put data");
    req.Execute("PUT", TEST_URL ... "/echo", OnHttpPut);
}

public void OnHttpPut(WebRequest req, int curlcode, int httpcode, int size) {
    g_http_pending--;
    AssertEq(curlcode, 0, "HTTP PUT curlcode");
    AssertEq(httpcode, 200, "HTTP PUT httpcode");

    char buf[256];
    req.GetString(buf, sizeof(buf));
    AssertStrEq(buf, "put data", "HTTP PUT echo body");


    MaybeFinishAll();
}

void Test_HTTP_Patch() {
    g_http_pending++;
    WebRequest req = NewRequest();
    req.SetBodyString("patch data");
    req.Execute("PATCH", TEST_URL ... "/echo", OnHttpPatch);
}

public void OnHttpPatch(WebRequest req, int curlcode, int httpcode, int size) {
    g_http_pending--;
    AssertEq(curlcode, 0, "HTTP PATCH curlcode");
    AssertEq(httpcode, 200, "HTTP PATCH httpcode");

    char buf[256];
    req.GetString(buf, sizeof(buf));
    AssertStrEq(buf, "patch data", "HTTP PATCH echo body");


    MaybeFinishAll();
}

void Test_HTTP_Timeout() {
    g_http_pending++;
    WebRequest req = NewRequest();
    req.SetOptInt(CURLOPT_TIMEOUT_MS, 500);
    req.Execute("GET", TEST_URL ... "/slow", OnHttpTimeout);
}

public void OnHttpTimeout(WebRequest req, int curlcode, int httpcode, int size) {
    g_http_pending--;
    // curlcode 28 = CURLE_OPERATION_TIMEDOUT
    Assert(curlcode != 0, "HTTP timeout curlcode non-zero");

    MaybeFinishAll();
}

void Test_HTTP_MultiRedirect() {
    g_http_pending++;
    WebRequest req = NewRequest();
    req.Execute("GET", TEST_URL ... "/multi-redirect", OnHttpMultiRedirect);
}

public void OnHttpMultiRedirect(WebRequest req, int curlcode, int httpcode, int size) {
    g_http_pending--;
    AssertEq(curlcode, 0, "HTTP multi-redirect curlcode");
    AssertEq(httpcode, 200, "HTTP multi-redirect followed to 200");

    MaybeFinishAll();
}

void Test_HTTP_MultipleHeaders() {
    g_http_pending++;
    WebRequest req = NewRequest();
    req.SetHeader("X-First", "one");
    req.SetHeader("X-Second", "two");
    req.SetHeader("X-Third", "three");
    req.Execute("GET", TEST_URL ... "/headers", OnHttpMultipleHeaders);
}

public void OnHttpMultipleHeaders(WebRequest req, int curlcode, int httpcode, int size) {
    g_http_pending--;
    AssertEq(curlcode, 0, "HTTP multi-header curlcode");

    Json json = Json.ParseResponse(req);
    Json headers = json.GetObject("headers");

    char val[128];
    headers.GetString("X-First", val, sizeof(val));
    AssertStrEq(val, "one", "HTTP multi-header X-First");
    headers.GetString("X-Second", val, sizeof(val));
    AssertStrEq(val, "two", "HTTP multi-header X-Second");
    headers.GetString("X-Third", val, sizeof(val));
    AssertStrEq(val, "three", "HTTP multi-header X-Third");

    headers.Close();
    json.Close();

    MaybeFinishAll();
}

void Test_HTTP_LargePost() {
    // Tests that large POST bodies don't stall due to Expect: 100-continue.
    // Without the Expect: "" fix, nginx/cloudflare proxies would hang here.
    g_http_pending++;
    Json body = Json.CreateObject();
    // Build a body larger than 1024 bytes (curl's Expect threshold)
    char longval[512];
    for (int i = 0; i < sizeof(longval) - 1; i++)
        longval[i] = 'A';
    longval[sizeof(longval) - 1] = '\0';
    body.SetString("field1", longval);
    body.SetString("field2", longval);
    body.SetString("field3", longval);

    WebRequest req = NewRequest();
    req.SetBodyJSON(body);
    // Short timeout so if Expect: 100-continue hangs, it fails fast
    req.SetOptInt(CURLOPT_TIMEOUT_MS, 3000);
    req.Execute("POST", TEST_URL ... "/post", OnHttpLargePost);
    body.Close();
}

public void OnHttpLargePost(WebRequest req, int curlcode, int httpcode, int size) {
    g_http_pending--;
    AssertEq(curlcode, 0, "HTTP large POST curlcode (Expect fix)");
    AssertEq(httpcode, 200, "HTTP large POST httpcode");
    Assert(size > 0, "HTTP large POST has body");

    MaybeFinishAll();
}

void Test_HTTP_CloseInFlight() {
    g_http_pending++;
    WebRequest req = NewRequest();
    req.Execute("GET", TEST_URL ... "/slow", OnHttpCloseInFlight);
    req.Close();  // Close immediately while request is in-flight
    g_passed++;   // didn't crash
}

public void OnHttpCloseInFlight(WebRequest req, int curlcode, int httpcode, int size) {
    g_http_pending--;
    // Callback fires with CURLE_ABORTED_BY_CALLBACK (42) because we closed in-flight
    AssertEq(curlcode, 42, "HTTP CloseInFlight curlcode is ABORTED_BY_CALLBACK");
    MaybeFinishAll();
}

void Test_HTTP_QueryString() {
    g_http_pending++;
    WebRequest req = NewRequest();
    req.Execute("GET", TEST_URL ... "/get?foo=bar&num=42", OnHttpQueryString);
}

public void OnHttpQueryString(WebRequest req, int curlcode, int httpcode, int size) {
    g_http_pending--;
    AssertEq(curlcode, 0, "HTTP query string curlcode");
    AssertEq(httpcode, 200, "HTTP query string httpcode");

    Json json = Json.ParseResponse(req);
    Json args = json.GetObject("args");
    char val[64];
    args.GetString("foo", val, sizeof(val));
    AssertStrEq(val, "bar", "HTTP query string foo=bar");
    args.GetString("num", val, sizeof(val));
    AssertStrEq(val, "42", "HTTP query string num=42");

    args.Close();
    json.Close();

    MaybeFinishAll();
}

void Test_HTTP_Head() {
    g_http_pending++;
    WebRequest req = NewRequest();
    req.SetOptInt(CURLOPT_NOBODY, 1);
    req.Execute("GET", TEST_URL ... "/json", OnHttpHead);
}

public void OnHttpHead(WebRequest req, int curlcode, int httpcode, int size) {
    g_http_pending--;
    AssertEq(curlcode, 0, "HTTP HEAD curlcode");
    AssertEq(httpcode, 200, "HTTP HEAD httpcode");
    AssertEq(size, 0, "HTTP HEAD no body");

    MaybeFinishAll();
}

void Test_HTTP_PostNoBody() {
    g_http_pending++;
    WebRequest req = NewRequest();
    req.Execute("POST", TEST_URL ... "/post", OnHttpPostNoBody);
}

public void OnHttpPostNoBody(WebRequest req, int curlcode, int httpcode, int size) {
    g_http_pending--;
    AssertEq(curlcode, 0, "HTTP POST no body curlcode");
    AssertEq(httpcode, 200, "HTTP POST no body httpcode");

    Json json = Json.ParseResponse(req);
    AssertEq(json.GetInt("size"), 0, "HTTP POST no body size=0");

    json.Close();

    MaybeFinishAll();
}

void Test_HTTP_GetRawData() {
    g_http_pending++;
    WebRequest req = NewRequest();
    req.Execute("GET", TEST_URL ... "/json", OnHttpGetRawData);
}

public void OnHttpGetRawData(WebRequest req, int curlcode, int httpcode, int size) {
    g_http_pending--;
    AssertEq(curlcode, 0, "HTTP GetRawData curlcode");

    int reported_size = req.ResponseLength;
    Assert(reported_size > 0, "HTTP GetRawData ResponseLength > 0");

    char buf[1024];
    int copied = req.GetRawData(buf, sizeof(buf));
    AssertEq(copied, reported_size, "HTTP GetRawData copied == ResponseLength");

    // Verify it's valid JSON by parsing via GetString
    char buf2[1024];
    req.GetString(buf2, sizeof(buf2));
    Assert(strlen(buf2) > 0, "HTTP GetString also works");


    MaybeFinishAll();
}

void Test_HTTP_GetInfoInt() {
    g_http_pending++;
    WebRequest req = NewRequest();
    req.Execute("GET", TEST_URL ... "/status/201", OnHttpGetInfoInt);
}

public void OnHttpGetInfoInt(WebRequest req, int curlcode, int httpcode, int size) {
    g_http_pending--;
    AssertEq(curlcode, 0, "HTTP GetInfoInt curlcode");

    int code;
    int ret = req.GetInfoInt(CURLINFO_RESPONSE_CODE, code);
    AssertEq(ret, 0, "HTTP GetInfoInt returns 0");
    AssertEq(code, 201, "HTTP GetInfoInt response code 201");
    AssertEq(httpcode, 201, "HTTP GetInfoInt httpcode matches");


    MaybeFinishAll();
}

void Test_HTTP_OverwriteHeader() {
    g_http_pending++;
    WebRequest req = NewRequest();
    req.SetHeader("X-Test", "first");
    req.SetHeader("X-Test", "second");
    req.Execute("GET", TEST_URL ... "/headers", OnHttpOverwriteHeader);
}

public void OnHttpOverwriteHeader(WebRequest req, int curlcode, int httpcode, int size) {
    g_http_pending--;
    AssertEq(curlcode, 0, "HTTP overwrite header curlcode");

    Json json = Json.ParseResponse(req);
    Json headers = json.GetObject("headers");
    char val[128];
    headers.GetString("X-Test", val, sizeof(val));
    AssertStrEq(val, "second", "HTTP overwrite header replaces by key");

    headers.Close();
    json.Close();

    MaybeFinishAll();
}

void Test_HTTP_RemoveHeader() {
    g_http_pending++;
    WebRequest req = NewRequest();
    req.SetHeader("X-Keep", "yes");
    req.SetHeader("X-Remove", "bye");
    req.RemoveHeader("X-Remove");
    req.Execute("GET", TEST_URL ... "/headers", OnHttpRemoveHeader);
}

public void OnHttpRemoveHeader(WebRequest req, int curlcode, int httpcode, int size) {
    g_http_pending--;
    AssertEq(curlcode, 0, "HTTP remove header curlcode");

    Json json = Json.ParseResponse(req);
    Json headers = json.GetObject("headers");
    char val[128];

    headers.GetString("X-Keep", val, sizeof(val));
    AssertStrEq(val, "yes", "HTTP RemoveHeader keeps other headers");

    headers.GetString("X-Remove", val, sizeof(val));
    AssertStrEq(val, "", "HTTP RemoveHeader removes target header");

    headers.Close();
    json.Close();
    MaybeFinishAll();
}

void Test_HTTP_ClearHeaders() {
    g_http_pending++;
    WebRequest req = NewRequest();
    req.SetHeader("X-Gone-One", "a");
    req.SetHeader("X-Gone-Two", "b");
    req.ClearHeaders();
    // Re-disable SSL verify after ClearHeaders (NewRequest sets these as curl opts, not headers)
    req.Execute("GET", TEST_URL ... "/headers", OnHttpClearHeaders);
}

public void OnHttpClearHeaders(WebRequest req, int curlcode, int httpcode, int size) {
    g_http_pending--;
    AssertEq(curlcode, 0, "HTTP clear headers curlcode");

    Json json = Json.ParseResponse(req);
    Json headers = json.GetObject("headers");
    char val[128];

    headers.GetString("X-Gone-One", val, sizeof(val));
    AssertStrEq(val, "", "HTTP ClearHeaders removes X-Gone-One");

    headers.GetString("X-Gone-Two", val, sizeof(val));
    AssertStrEq(val, "", "HTTP ClearHeaders removes X-Gone-Two");

    headers.Close();
    json.Close();
    MaybeFinishAll();
}

void Test_HTTP_CaseInsensitiveHeader() {
    g_http_pending++;
    WebRequest req = NewRequest();
    req.SetHeader("X-Case-Test", "first");
    req.SetHeader("x-case-test", "second");  // same key, different case
    req.Execute("GET", TEST_URL ... "/headers", OnHttpCaseHeader);
}

public void OnHttpCaseHeader(WebRequest req, int curlcode, int httpcode, int size) {
    g_http_pending--;
    AssertEq(curlcode, 0, "HTTP case insensitive header curlcode");

    Json json = Json.ParseResponse(req);
    Json headers = json.GetObject("headers");
    char val[128];
    headers.GetString("X-Case-Test", val, sizeof(val));
    AssertStrEq(val, "second", "HTTP SetHeader is case-insensitive (replaces)");

    headers.Close();
    json.Close();
    MaybeFinishAll();
}

void Test_HTTP_DNS_Fail() {
    g_http_pending++;
    WebRequest req = NewRequest();
    req.SetOptInt(CURLOPT_CONNECTTIMEOUT_MS, 2000);
    req.Execute("GET", "http://this.domain.does.not.exist.invalid/test", OnHttpDnsFail);
}

public void OnHttpDnsFail(WebRequest req, int curlcode, int httpcode, int size) {
    g_http_pending--;
    // curlcode 6 = CURLE_COULDNT_RESOLVE_HOST
    Assert(curlcode != 0, "HTTP DNS fail curlcode non-zero");

    MaybeFinishAll();
}

void Test_HTTP_SetBody() {
    g_http_pending++;
    WebRequest req = NewRequest();
    char body[16];
    body[0] = 'r';
    body[1] = 'a';
    body[2] = 'w';
    req.SetBody(body, 3);
    req.Execute("POST", TEST_URL ... "/echo", OnHttpSetBody);
}

public void OnHttpSetBody(WebRequest req, int curlcode, int httpcode, int size) {
    g_http_pending--;
    AssertEq(curlcode, 0, "HTTP SetBody curlcode");
    AssertEq(httpcode, 200, "HTTP SetBody httpcode");

    char buf[256];
    req.GetString(buf, sizeof(buf));
    AssertStrEq(buf, "raw", "HTTP SetBody echo body");


    MaybeFinishAll();
}

void Test_HTTP_GetInfoString() {
    g_http_pending++;
    WebRequest req = NewRequest();
    req.Execute("GET", TEST_URL ... "/json", OnHttpGetInfoString);
}

public void OnHttpGetInfoString(WebRequest req, int curlcode, int httpcode, int size) {
    g_http_pending--;
    AssertEq(curlcode, 0, "HTTP GetInfoString curlcode");

    char ct[256];
    int ret = req.GetInfoString(CURLINFO_CONTENT_TYPE, ct, sizeof(ct));
    AssertEq(ret, 0, "HTTP GetInfoString returns 0");
    Assert(StrContains(ct, "application/json") != -1, "HTTP GetInfoString content-type is json");


    MaybeFinishAll();
}

void Test_HTTP_GetErrorMessage() {
    g_http_pending++;
    WebRequest req = NewRequest();
    req.SetOptInt(CURLOPT_CONNECTTIMEOUT_MS, 1000);
    req.Execute("GET", "http://127.0.0.1:1/nope", OnHttpGetErrorMessage);
}

public void OnHttpGetErrorMessage(WebRequest req, int curlcode, int httpcode, int size) {
    g_http_pending--;
    Assert(curlcode != 0, "HTTP GetErrorMessage curlcode non-zero");

    char errmsg[CURL_ERROR_SIZE];
    req.GetErrorMessage(errmsg, sizeof(errmsg));
    Assert(strlen(errmsg) > 0, "HTTP GetErrorMessage non-empty");


    MaybeFinishAll();
}

void Test_HTTP_SetGlobalOpt() {
    // Set pipelining mode (already the default) — just verify no crash
    int ret = async2_SetMultiOpt(CURLMOPT_PIPELINING, 1);
    AssertEq(ret, 0, "SetGlobalOpt returns 0");
}

void Test_HTTP_MsgPackParse() {
    g_http_pending++;
    Json body = Json.CreateObject();
    body.SetString("key", "value");
    body.SetInt("num", 123);

    WebRequest req = NewRequest();
    req.SetBodyMsgPack(body);
    req.Execute("POST", TEST_URL ... "/msgpack-echo", OnHttpMsgPackParse);
    body.Close();
}

public void OnHttpMsgPackParse(WebRequest req, int curlcode, int httpcode, int size) {
    g_http_pending--;
    AssertEq(curlcode, 0, "HTTP MsgPackParse curlcode");
    AssertEq(httpcode, 200, "HTTP MsgPackParse httpcode");

    Json parsed = async2_MsgPackParse(req);
    Assert(view_as<int>(parsed) != 0, "HTTP MsgPackParse returns handle");

    char val[64];
    parsed.GetString("key", val, sizeof(val));
    AssertStrEq(val, "value", "HTTP MsgPackParse key");
    AssertEq(parsed.GetInt("num"), 123, "HTTP MsgPackParse num");

    parsed.Close();

    MaybeFinishAll();
}

void Test_HTTP_Version() {
    char version[256];
    async2_CurlVersion(version, sizeof(version));
    Assert(strlen(version) > 0, "Version string non-empty");
    Assert(StrContains(version, "libcurl") != -1, "Version contains libcurl");
}

void Test_HTTP_ErrorString() {
    char buf[256];
    // curlcode 0 = CURLE_OK
    async2_CurlErrorString(0, buf, sizeof(buf));
    Assert(strlen(buf) > 0, "ErrorString for code 0 non-empty");

    // curlcode 7 = CURLE_COULDNT_CONNECT
    async2_CurlErrorString(7, buf, sizeof(buf));
    Assert(strlen(buf) > 0, "ErrorString for code 7 non-empty");
}

void Test_HTTP_CurlOpt() {
    WebRequest req = NewRequest();
    int ret = req.SetOptInt(CURLOPT_TIMEOUT, 30);
    AssertEq(ret, 0, "SetOptInt TIMEOUT returns 0");

    ret = req.SetOptString(CURLOPT_USERAGENT, "async2-test/1.0");
    AssertEq(ret, 0, "SetOptString USERAGENT returns 0");

    req.Close();
}

void Test_HTTP_CloseBeforeExecute() {
    WebRequest req = NewRequest();
    Assert(view_as<int>(req) != 0, "New request non-zero");
    req.SetHeader("X-Foo", "bar");
    req.SetBodyString("test");
    req.Close();  // Close without Execute — must still free the handle

    g_passed++;  // didn't crash
}

// ============================================================================
// Feature Tests: SetBodyJSON deep copy, SetResponseType, SetClient
// ============================================================================

// Test that SetBodyJSON deep-copies: mutating the original after SetBodyJSON
// must not affect the body sent to the server.
void Test_HTTP_SetBodyJSON_DeepCopy() {
    g_http_pending++;
    Json body = Json.CreateObject();
    body.SetString("key", "original");
    body.SetInt("num", 100);

    WebRequest req = NewRequest();
    req.SetBodyJSON(body);

    // Mutate original AFTER SetBodyJSON — should not affect the request
    body.SetString("key", "mutated");
    body.SetInt("num", 999);

    req.Execute("POST", TEST_URL ... "/post", OnHttpSetBodyJSON_DeepCopy);
    body.Close();
}

public void OnHttpSetBodyJSON_DeepCopy(WebRequest req, int curlcode, int httpcode, int size) {
    g_http_pending--;
    AssertEq(curlcode, 0, "SetBodyJSON DeepCopy curlcode");
    AssertEq(httpcode, 200, "SetBodyJSON DeepCopy httpcode");

    Json json = Json.ParseResponse(req);
    Assert(view_as<int>(json) != 0, "SetBodyJSON DeepCopy parse");

    Json data = json.GetObject("data");
    Assert(view_as<int>(data) != 0, "SetBodyJSON DeepCopy data obj");

    // Must see original values, not mutated ones
    char val[64];
    data.GetString("key", val, sizeof(val));
    AssertStrEq(val, "original", "SetBodyJSON DeepCopy key is original");
    AssertEq(data.GetInt("num"), 100, "SetBodyJSON DeepCopy num is 100");

    data.Close();
    json.Close();
    MaybeFinishAll();
}

// Test SetBodyJSON + compress: deep-copied body gets serialized then compressed
// on the event thread.
void Test_HTTP_SetBodyJSON_Compress() {
    g_http_pending++;
    Json body = Json.CreateObject();
    // Large repetitive value to ensure deflate actually shrinks it
    char big[512];
    for (int i = 0; i < sizeof(big) - 1; i++)
        big[i] = 'X';
    big[sizeof(big) - 1] = '\0';
    body.SetString("padding", big);

    WebRequest req = NewRequest();
    req.SetBodyJSON(body);
    req.SetCompression(true);
    req.Execute("POST", TEST_URL ... "/deflate", OnHttpSetBodyJSON_Compress);
    body.Close();
}

public void OnHttpSetBodyJSON_Compress(WebRequest req, int curlcode, int httpcode, int size) {
    g_http_pending--;
    AssertEq(curlcode, 0, "SetBodyJSON Compress curlcode");
    AssertEq(httpcode, 200, "SetBodyJSON Compress httpcode");

    Json json = Json.ParseResponse(req);
    Assert(view_as<int>(json) != 0, "SetBodyJSON Compress parse");

    int compressed = json.GetInt("compressed_size");
    int decompressed = json.GetInt("decompressed_size");
    Assert(compressed < decompressed, "SetBodyJSON Compress actually compressed");
    Assert(decompressed > 500, "SetBodyJSON Compress decompressed is large");

    json.Close();
    MaybeFinishAll();
}

// Test SetBodyMsgPack deep copy: same pattern as JSON.
void Test_HTTP_SetBodyMsgPack_DeepCopy() {
    g_http_pending++;
    Json body = Json.CreateObject();
    body.SetString("key", "original");
    body.SetInt("num", 200);

    WebRequest req = NewRequest();
    req.SetBodyMsgPack(body);

    // Mutate after SetBodyMsgPack
    body.SetString("key", "mutated");
    body.SetInt("num", 888);

    req.Execute("POST", TEST_URL ... "/msgpack-echo", OnHttpSetBodyMsgPack_DeepCopy);
    body.Close();
}

public void OnHttpSetBodyMsgPack_DeepCopy(WebRequest req, int curlcode, int httpcode, int size) {
    g_http_pending--;
    AssertEq(curlcode, 0, "SetBodyMsgPack DeepCopy curlcode");
    AssertEq(httpcode, 200, "SetBodyMsgPack DeepCopy httpcode");

    Json parsed = async2_MsgPackParse(req);
    Assert(view_as<int>(parsed) != 0, "SetBodyMsgPack DeepCopy parse");

    char val[64];
    parsed.GetString("key", val, sizeof(val));
    AssertStrEq(val, "original", "SetBodyMsgPack DeepCopy key is original");
    AssertEq(parsed.GetInt("num"), 200, "SetBodyMsgPack DeepCopy num is 200");

    parsed.Close();
    MaybeFinishAll();
}

// Test SetResponseType(RESPONSE_JSON): parsed Json handle delivered directly in callback.
void Test_HTTP_SetResponseType() {
    g_http_pending++;
    WebRequest req = NewRequest();
    req.SetResponseType(RESPONSE_JSON);
    req.Execute("GET", TEST_URL ... "/json", OnHttpSetResponseType);
}

public void OnHttpSetResponseType(WebRequest req, int curlcode, int httpcode, Json data) {
    g_http_pending--;
    AssertEq(curlcode, 0, "SetResponseType curlcode");
    AssertEq(httpcode, 200, "SetResponseType httpcode");

    Assert(view_as<int>(data) != 0, "SetResponseType data handle is valid");

    char name[64];
    data.GetString("name", name, sizeof(name));
    AssertStrEq(name, "async2", "SetResponseType json.name");
    AssertEq(data.GetInt("version"), 1, "SetResponseType json.version");

    data.Close();
    MaybeFinishAll();
}

// Test SetResponseType with non-JSON response: parse fails, data is null handle,
// raw body still accessible via GetString.
void Test_HTTP_SetResponseType_Fallback() {
    g_http_pending++;
    WebRequest req = NewRequest();
    req.SetResponseType(RESPONSE_JSON);
    req.SetBodyString("plain text body");
    req.Execute("POST", TEST_URL ... "/echo", OnHttpSetResponseType_Fallback);
}

public void OnHttpSetResponseType_Fallback(WebRequest req, int curlcode, int httpcode, Json data) {
    g_http_pending--;
    AssertEq(curlcode, 0, "SetResponseType Fallback curlcode");
    AssertEq(httpcode, 200, "SetResponseType Fallback httpcode");

    // Non-JSON response — parse fails, data is null handle
    AssertEq(view_as<int>(data), 0, "SetResponseType Fallback data is null");

    // Raw body still accessible
    char buf[256];
    req.GetString(buf, sizeof(buf));
    AssertStrEq(buf, "plain text body", "SetResponseType Fallback raw body preserved");

    MaybeFinishAll();
}

// Test SetClient (synchronous): set and clear client index, close before execute.
void Test_HTTP_SetClient_Sync() {
    WebRequest req = NewRequest();
    int ret = async2_SetOwner(req, 1);
    AssertEq(ret, 0, "SetClient returns 0");

    // Clear client
    ret = async2_SetOwner(req, 0);
    AssertEq(ret, 0, "SetClient clear returns 0");

    req.Close();
    g_passed++;  // didn't crash
}

// Test SetClient auto-cancel on disconnect: add a bot, associate a slow request
// with the bot's client index, kick the bot, verify callback fires with curlcode 42.
void Test_HTTP_SetClient_Disconnect() {
    g_http_pending++;
    ServerCommand("bot -name async2testbot");
    // Bot creation needs a frame to complete — wait 0.1s then find it
    CreateTimer(0.1, Timer_SetClient_FindBot);
}

public Action Timer_SetClient_FindBot(Handle timer) {
    int bot = -1;
    for (int i = 1; i <= MaxClients; i++) {
        if (IsClientConnected(i) && IsFakeClient(i)) {
            bot = i;
            break;
        }
    }
    if (bot == -1) {
        g_http_pending--;
        PrintToServer("[SKIP] SetClient Disconnect: no bot available (server may not support bots)");
        MaybeFinishAll();
        return Plugin_Stop;
    }

    // Send a slow request associated with this bot
    WebRequest req = NewRequest();
    req.SetOwner(bot);
    req.Execute("GET", TEST_URL ... "/slow", OnHttpSetClient_Disconnect);

    // Kick the bot after a short delay so the request is in-flight
    CreateTimer(0.2, Timer_KickBot, GetClientUserId(bot));
    return Plugin_Stop;
}

public Action Timer_KickBot(Handle timer, int userid) {
    int client = GetClientOfUserId(userid);
    if (client > 0 && IsClientConnected(client)) {
        KickClient(client, "async2 test");
    }
    return Plugin_Stop;
}

public void OnHttpSetClient_Disconnect(WebRequest req, int curlcode, int httpcode, int size) {
    g_http_pending--;
    // Bot was kicked → OnClientDisconnecting → auto-cancel → curlcode 42
    AssertEq(curlcode, 42, "SetClient Disconnect curlcode is ABORTED_BY_CALLBACK");
    MaybeFinishAll();
}

// Test SetBodyJSON overwrites previous SetBody
void Test_HTTP_SetBodyJSON_OverwriteRaw() {
    g_http_pending++;
    WebRequest req = NewRequest();
    req.SetBodyString("should be overwritten");
    Json body = Json.CreateObject();
    body.SetString("source", "json");
    req.SetBodyJSON(body);
    req.Execute("POST", TEST_URL ... "/post", OnHttpSetBodyJSON_OverwriteRaw);
    body.Close();
}

public void OnHttpSetBodyJSON_OverwriteRaw(WebRequest req, int curlcode, int httpcode, int size) {
    g_http_pending--;
    AssertEq(curlcode, 0, "SetBodyJSON Overwrite curlcode");

    Json json = Json.ParseResponse(req);
    Json data = json.GetObject("data");
    char val[64];
    data.GetString("source", val, sizeof(val));
    AssertStrEq(val, "json", "SetBodyJSON Overwrite body is JSON not raw");

    data.Close();
    json.Close();
    MaybeFinishAll();
}

// Test SetBody overwrites previous SetBodyJSON
void Test_HTTP_SetBody_OverwriteJSON() {
    g_http_pending++;
    Json body = Json.CreateObject();
    body.SetString("source", "json");
    WebRequest req = NewRequest();
    req.SetBodyJSON(body);
    req.SetBodyString("raw wins");  // Should overwrite the JSON body
    req.Execute("POST", TEST_URL ... "/echo", OnHttpSetBody_OverwriteJSON);
    body.Close();
}

public void OnHttpSetBody_OverwriteJSON(WebRequest req, int curlcode, int httpcode, int size) {
    g_http_pending--;
    AssertEq(curlcode, 0, "SetBody OverwriteJSON curlcode");

    char buf[256];
    req.GetString(buf, sizeof(buf));
    AssertStrEq(buf, "raw wins", "SetBody OverwriteJSON body is raw not JSON");

    MaybeFinishAll();
}

// ============================================================================
// Edge Case Tests
// ============================================================================

// SetBodyJSON with nested objects: deep copy must recurse into children.
void Test_HTTP_SetBodyJSON_Nested() {
    g_http_pending++;
    Json inner = Json.CreateObject();
    inner.SetString("deep", "value");
    inner.SetInt("n", 7);

    Json arr = Json.CreateArray();
    arr.ArrayAppendInt(1);
    arr.ArrayAppendInt(2);

    Json body = Json.CreateObject();
    body.SetObject("inner", inner);
    body.SetObject("list", arr);

    WebRequest req = NewRequest();
    req.SetBodyJSON(body);

    // Mutate originals after SetBodyJSON
    inner.SetString("deep", "MUTATED");
    arr.ArrayAppendInt(999);

    req.Execute("POST", TEST_URL ... "/post", OnHttpSetBodyJSON_Nested);
    inner.Close();
    arr.Close();
    body.Close();
}

public void OnHttpSetBodyJSON_Nested(WebRequest req, int curlcode, int httpcode, int size) {
    g_http_pending--;
    AssertEq(curlcode, 0, "SetBodyJSON Nested curlcode");

    Json json = Json.ParseResponse(req);
    Json data = json.GetObject("data");

    Json inner = data.GetObject("inner");
    char val[64];
    inner.GetString("deep", val, sizeof(val));
    AssertStrEq(val, "value", "SetBodyJSON Nested inner.deep is original");
    AssertEq(inner.GetInt("n"), 7, "SetBodyJSON Nested inner.n");

    Json list = data.GetArray("list");
    AssertEq(list.ArrayLength, 2, "SetBodyJSON Nested list length is 2 not 3");

    list.Close();
    inner.Close();
    data.Close();
    json.Close();
    MaybeFinishAll();
}

// SetBodyJSON called twice: second call must replace the first.
void Test_HTTP_SetBodyJSON_CalledTwice() {
    g_http_pending++;
    Json first = Json.CreateObject();
    first.SetString("which", "first");
    Json second = Json.CreateObject();
    second.SetString("which", "second");

    WebRequest req = NewRequest();
    req.SetBodyJSON(first);
    req.SetBodyJSON(second);  // Must replace the first
    req.Execute("POST", TEST_URL ... "/post", OnHttpSetBodyJSON_CalledTwice);
    first.Close();
    second.Close();
}

public void OnHttpSetBodyJSON_CalledTwice(WebRequest req, int curlcode, int httpcode, int size) {
    g_http_pending--;
    AssertEq(curlcode, 0, "SetBodyJSON CalledTwice curlcode");

    Json json = Json.ParseResponse(req);
    Json data = json.GetObject("data");
    char val[64];
    data.GetString("which", val, sizeof(val));
    AssertStrEq(val, "second", "SetBodyJSON CalledTwice body is second");

    data.Close();
    json.Close();
    MaybeFinishAll();
}

// SetBodyJSON with invalid handle: should return error.
void Test_HTTP_SetBodyJSON_InvalidHandle() {
    WebRequest req = NewRequest();
    int ret = async2_SetBodyJSON(req, view_as<Json>(0));
    AssertEq(ret, 2, "SetBodyJSON null handle returns 2");
    ret = async2_SetBodyJSON(req, view_as<Json>(99999));
    AssertEq(ret, 2, "SetBodyJSON invalid handle returns 2");
    req.Close();
}

// SetResponseType + empty body: data is null handle.
void Test_HTTP_SetResponseType_EmptyBody() {
    g_http_pending++;
    WebRequest req = NewRequest();
    req.SetResponseType(RESPONSE_JSON);
    req.Execute("GET", TEST_URL ... "/empty", OnHttpSetResponseType_EmptyBody);
}

public void OnHttpSetResponseType_EmptyBody(WebRequest req, int curlcode, int httpcode, Json data) {
    g_http_pending--;
    AssertEq(curlcode, 0, "SetResponseType EmptyBody curlcode");
    AssertEq(httpcode, 200, "SetResponseType EmptyBody httpcode");
    AssertEq(view_as<int>(data), 0, "SetResponseType EmptyBody data is null");

    MaybeFinishAll();
}

// SetResponseType + curl error: data is null handle.
void Test_HTTP_SetResponseType_CurlError() {
    g_http_pending++;
    WebRequest req = NewRequest();
    req.SetResponseType(RESPONSE_JSON);
    req.SetOptInt(CURLOPT_TIMEOUT_MS, 500);
    req.Execute("GET", "https://127.0.0.1:1/nope", OnHttpSetResponseType_CurlError);
}

public void OnHttpSetResponseType_CurlError(WebRequest req, int curlcode, int httpcode, Json data) {
    g_http_pending--;
    Assert(curlcode != 0, "SetResponseType CurlError curlcode is non-zero");
    AssertEq(view_as<int>(data), 0, "SetResponseType CurlError data is null");

    MaybeFinishAll();
}

// SetResponseType + JSON array root: non-object root should parse fine.
void Test_HTTP_SetResponseType_Array() {
    g_http_pending++;
    WebRequest req = NewRequest();
    req.SetResponseType(RESPONSE_JSON);
    req.Execute("GET", TEST_URL ... "/array", OnHttpSetResponseType_Array);
}

public void OnHttpSetResponseType_Array(WebRequest req, int curlcode, int httpcode, Json data) {
    g_http_pending--;
    AssertEq(curlcode, 0, "SetResponseType Array curlcode");

    Assert(view_as<int>(data) != 0, "SetResponseType Array data is valid");
    AssertEq(view_as<int>(data.Type), view_as<int>(JSON_TYPE_ARRAY), "SetResponseType Array type is ARRAY");
    AssertEq(data.ArrayLength, 6, "SetResponseType Array length");
    AssertEq(data.ArrayGetInt(0), 1, "SetResponseType Array[0]");

    data.Close();
    MaybeFinishAll();
}

// SetClient with multiple requests: all should be cancelled when bot disconnects.
int g_multi_cancel_count;

void Test_HTTP_SetClient_MultiCancel() {
    g_http_pending++;
    g_multi_cancel_count = 0;
    ServerCommand("bot -name async2testbot2");
    CreateTimer(0.1, Timer_SetClient_MultiCancel);
}

public Action Timer_SetClient_MultiCancel(Handle timer) {
    int bot = -1;
    for (int i = 1; i <= MaxClients; i++) {
        if (IsClientConnected(i) && IsFakeClient(i)) {
            bot = i;
            break;
        }
    }
    if (bot == -1) {
        g_http_pending--;
        PrintToServer("[SKIP] SetClient MultiCancel: no bot available");
        MaybeFinishAll();
        return Plugin_Stop;
    }

    // Send 3 slow requests all associated with this bot
    for (int i = 0; i < 3; i++) {
        g_http_pending++;
        WebRequest req = NewRequest();
        req.SetOwner(bot);
        req.Execute("GET", TEST_URL ... "/slow", OnHttpSetClient_MultiCancel);
    }
    // We added 3, remove the 1 we added in Test_HTTP_SetClient_MultiCancel
    g_http_pending--;

    // Kick after short delay
    CreateTimer(0.2, Timer_KickBot, GetClientUserId(bot));
    return Plugin_Stop;
}

public void OnHttpSetClient_MultiCancel(WebRequest req, int curlcode, int httpcode, int size) {
    g_http_pending--;
    if (curlcode == 42)
        g_multi_cancel_count++;

    // Check after all 3 have fired
    if (g_multi_cancel_count == 3) {
        g_passed++;
        PrintToServer("[PASS] SetClient MultiCancel: all 3 requests cancelled");
    } else if (g_multi_cancel_count + g_http_pending <= 0) {
        // All done but not all cancelled
        g_failed++;
        PrintToServer("[FAIL] SetClient MultiCancel: only %d/3 cancelled", g_multi_cancel_count);
    }
    MaybeFinishAll();
}

// SetClient reassign: change from client A to client B, kicking A must NOT cancel.
void Test_HTTP_SetClient_Reassign() {
    g_http_pending++;
    ServerCommand("bot -name async2testbotA");
    ServerCommand("bot -name async2testbotB");
    CreateTimer(0.1, Timer_SetClient_Reassign);
}

public Action Timer_SetClient_Reassign(Handle timer) {
    // Find two bots
    int botA = -1, botB = -1;
    for (int i = 1; i <= MaxClients; i++) {
        if (IsClientConnected(i) && IsFakeClient(i)) {
            if (botA == -1)
                botA = i;
            else if (botB == -1)
                botB = i;
        }
    }
    if (botA == -1 || botB == -1) {
        g_http_pending--;
        PrintToServer("[SKIP] SetClient Reassign: need 2 bots");
        MaybeFinishAll();
        return Plugin_Stop;
    }

    // Create request, assign to botA, then reassign to botB
    WebRequest req = NewRequest();
    req.SetOwner(botA);
    req.SetOwner(botB);  // Reassign — botA should no longer track this
    req.Execute("GET", TEST_URL ... "/json", OnHttpSetClient_Reassign);

    // Kick botA — should NOT cancel the request (it's now on botB)
    CreateTimer(0.1, Timer_KickBot, GetClientUserId(botA));
    // Kick botB later to clean up (after request completes)
    CreateTimer(2.0, Timer_KickBot, GetClientUserId(botB));
    return Plugin_Stop;
}

public void OnHttpSetClient_Reassign(WebRequest req, int curlcode, int httpcode, int size) {
    g_http_pending--;
    // Request should complete normally (curlcode 0), not be cancelled (42)
    AssertEq(curlcode, 0, "SetClient Reassign curlcode is 0 (not cancelled by old client)");
    AssertEq(httpcode, 200, "SetClient Reassign httpcode");
    MaybeFinishAll();
}

// SetClient after Execute+Close: handle is freed, SetClient must return error.
void Test_HTTP_SetClient_AfterClose() {
    WebRequest req = NewRequest();
    req.Execute("GET", TEST_URL ... "/json", OnHttpSetClient_AfterClose_Sink);
    g_http_pending++;
    req.Close();
    int ret = async2_SetOwner(req, 1);
    AssertEq(ret, 2, "SetClient after Close returns 2 (invalid handle)");
}

public void OnHttpSetClient_AfterClose_Sink(WebRequest req, int curlcode, int httpcode, int size) {
    g_http_pending--;
    // We don't care about the result — just draining the callback
    MaybeFinishAll();
}

// SetResponseType + retry: first attempt 500 → retry → parse works on final response.
// Tests that PrepareForRetry clears response_node before re-parse.
void Test_HTTP_SetResponseType_Retry() {
    g_http_pending++;
    WebRequest req = NewRequest();
    req.SetResponseType(RESPONSE_JSON);
    req.SetRetry(1, 100);  // 1 retry, 100ms delay
    req.Execute("GET", TEST_URL ... "/status/500", OnHttpSetResponseType_Retry);
}

public void OnHttpSetResponseType_Retry(WebRequest req, int curlcode, int httpcode, Json data) {
    g_http_pending--;
    AssertEq(curlcode, 0, "SetResponseType Retry curlcode");
    AssertEq(httpcode, 500, "SetResponseType Retry httpcode is 500 (exhausted retries)");
    Assert(req.RetryCount > 0, "SetResponseType Retry count > 0");

    Assert(view_as<int>(data) != 0, "SetResponseType Retry data is valid");
    AssertEq(data.GetInt("status"), 500, "SetResponseType Retry parsed status field");
    data.Close();

    MaybeFinishAll();
}

// SetBodyJSON + Close without Execute: body_node must be freed, no pool leak.
void Test_HTTP_SetBodyJSON_CloseNoExecute() {
    int pool_total1, pool_used1, pool_bs1;
    async2_JsonPoolStats(pool_total1, pool_used1, pool_bs1);

    Json body = Json.CreateObject();
    body.SetString("leak", "test");
    body.SetInt("num", 42);

    WebRequest req = NewRequest();
    req.SetBodyJSON(body);
    body.Close();
    req.Close();  // Must free body_node without Execute

    int pool_total2, pool_used2, pool_bs2;
    async2_JsonPoolStats(pool_total2, pool_used2, pool_bs2);

    // Pool usage should return to same level (deep copy freed on Close)
    AssertEq(pool_used2, pool_used1, "SetBodyJSON CloseNoExecute no pool leak");
}

// SetResponseType + large response: /large returns ~100 keys, verify parse works.
void Test_HTTP_SetResponseType_Large() {
    g_http_pending++;
    WebRequest req = NewRequest();
    req.SetResponseType(RESPONSE_JSON);
    req.Execute("GET", TEST_URL ... "/large", OnHttpSetResponseType_Large);
}

public void OnHttpSetResponseType_Large(WebRequest req, int curlcode, int httpcode, Json data) {
    g_http_pending--;
    AssertEq(curlcode, 0, "SetResponseType Large curlcode");

    Assert(view_as<int>(data) != 0, "SetResponseType Large data is valid");
    AssertEq(view_as<int>(data.Type), view_as<int>(JSON_TYPE_OBJECT), "SetResponseType Large type is OBJECT");

    // /large returns 100 keys: key_0..key_99
    AssertEq(data.ObjectSize, 100, "SetResponseType Large has 100 keys");
    AssertEq(data.GetInt("key_0"), 0, "SetResponseType Large key_0");
    AssertEq(data.GetInt("key_99"), 99, "SetResponseType Large key_99");

    data.Close();
    MaybeFinishAll();
}

// JsonParse called repeatedly on non-JSON body: repeated calls must not crash.
void Test_HTTP_JsonParse_RepeatedFail() {
    g_http_pending++;
    WebRequest req = NewRequest();
    req.SetBodyString("not json");
    req.Execute("POST", TEST_URL ... "/echo", OnHttpJsonParse_RepeatedFail);
}

public void OnHttpJsonParse_RepeatedFail(WebRequest req, int curlcode, int httpcode, int size) {
    g_http_pending--;
    AssertEq(curlcode, 0, "JsonParse RepeatedFail curlcode");

    // Call JsonParse 3 times on non-JSON — all must return 0, no crash
    Json j1 = Json.ParseResponse(req);
    Json j2 = Json.ParseResponse(req);
    Json j3 = Json.ParseResponse(req);
    AssertEq(view_as<int>(j1), 0, "JsonParse RepeatedFail 1st returns 0");
    AssertEq(view_as<int>(j2), 0, "JsonParse RepeatedFail 2nd returns 0");
    AssertEq(view_as<int>(j3), 0, "JsonParse RepeatedFail 3rd returns 0");

    MaybeFinishAll();
}

void RunHttpTests() {
    // Synchronous tests (no network)
    Test_HTTP_Version();
    Test_HTTP_ErrorString();
    Test_HTTP_CurlOpt();
    Test_HTTP_CloseBeforeExecute();
    Test_HTTP_SetGlobalOpt();

    // Async tests (need test server)
    Test_HTTP_Get();
    Test_HTTP_PostJson();
    Test_HTTP_PostString();
    Test_HTTP_CustomHeader();
    Test_HTTP_Status404();
    Test_HTTP_Delete();
    Test_HTTP_EmptyBody();
    Test_HTTP_LargeResponse();
    Test_HTTP_Redirect();
    Test_HTTP_ConnRefused();
    Test_HTTP_ResponseHeader();
    Test_HTTP_Gzip();
    Test_HTTP_Deflate_Upload();
    Test_HTTP_PlainHTTP();
    Test_HTTP_Put();
    Test_HTTP_Patch();
    Test_HTTP_Timeout();
    Test_HTTP_MultiRedirect();
    Test_HTTP_MultipleHeaders();
    Test_HTTP_LargePost();
    Test_HTTP_CloseInFlight();
    Test_HTTP_QueryString();
    Test_HTTP_Head();
    Test_HTTP_PostNoBody();
    Test_HTTP_GetRawData();
    Test_HTTP_GetInfoInt();
    Test_HTTP_GetInfoString();
    Test_HTTP_GetErrorMessage();
    Test_HTTP_OverwriteHeader();
    Test_HTTP_RemoveHeader();
    Test_HTTP_ClearHeaders();
    Test_HTTP_CaseInsensitiveHeader();
    Test_HTTP_DNS_Fail();
    Test_HTTP_SetBody();
    Test_HTTP_MsgPackParse();

    // Feature tests: deep copy, pre-parse, client tracking
    Test_HTTP_SetClient_Sync();
    Test_HTTP_SetBodyJSON_DeepCopy();
    Test_HTTP_SetBodyJSON_Compress();
    Test_HTTP_SetBodyMsgPack_DeepCopy();
    Test_HTTP_SetResponseType();
    Test_HTTP_SetResponseType_Fallback();
    Test_HTTP_SetClient_Disconnect();
    Test_HTTP_SetBodyJSON_OverwriteRaw();
    Test_HTTP_SetBody_OverwriteJSON();

    // Edge case tests
    Test_HTTP_SetBodyJSON_Nested();
    Test_HTTP_SetBodyJSON_CalledTwice();
    Test_HTTP_SetBodyJSON_InvalidHandle();
    Test_HTTP_SetResponseType_EmptyBody();
    Test_HTTP_SetResponseType_CurlError();
    Test_HTTP_SetResponseType_Array();
    Test_HTTP_SetClient_MultiCancel();
    Test_HTTP_SetClient_Reassign();
    Test_HTTP_SetClient_AfterClose();
    Test_HTTP_SetResponseType_Retry();
    Test_HTTP_SetBodyJSON_CloseNoExecute();
    Test_HTTP_SetResponseType_Large();
    Test_HTTP_JsonParse_RepeatedFail();
}
