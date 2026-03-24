# API Version History

Version number returned by `async2_GetVersion()`. Extensions older than the
native return 0 (it is marked optional).

| Version | Feature |
|---------|---------|
| 1 | Baseline (implicit — before this native existed) |
| 2 | SetBodyJSON/SetBodyMsgPack/SetObject/WsSendJson/WsSendMsgPack auto-consume handles |
