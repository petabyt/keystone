# Keystone Fork

**Work done on this repo:**
- Upgrade cmake version and fix warnings
- Re-enabled all LLVM error messages (`Error()`)
- Adds `ks_set_error_message_handler` to handle error messages
- Fixes several RISC-V bugs (missing address increment, typos, fix for `la`)
- Implement `ks_set_instruction_stream_handler`
- Implement `ksApplyOptions` method for AsmParsers and `KS_MODE_RISCVC`

Most of the common bugs fixed include:
- Keystone devs forgot to pass `Address` along between instructions so it's reset to 0/not incremented
- Keystone devs forgot to increment address in the first place
- [Placing multiple lines on a single if statement with no braces](https://github.com/petabyt/keystone2/commit/5ae824cf5c9d64fa8c629eb023bea02bbf55b981#diff-5783687fdec2ca7ccfec701317ee45f83a7602da0d583004c5e053a7793f197cR204-R215)
