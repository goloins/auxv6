# testdaemon(1)

## Name

testdaemon - test double-fork daemonization strategy

## Synopsis

```
testdaemon
```

## Description

`testdaemon` tests the double-fork + setsid + stdio-redirect daemonization 
pattern used by `devman -d`. It is a minimal diagnostic tool to isolate 
whether kernel process management, session/group handling, or the 
double-fork pattern itself is responsible for any issues.

The program:
1. Forks once; parent exits
2. Calls setsid() to create a new session
3. Forks a second time; intermediate child exits  
4. Final grandchild daemon redirects stdin/stdout/stderr to /dev/null
5. Writes periodic status dots to `/tmp/testdaemon.log` for 30 seconds
6. Exits normally

The daemon will be reparented to init (PID 1) after the intermediate 
child exits.

## Exit Status

Returns 0 on successful daemonization and completion.
Returns 1 on fork or I/O failures.

## Files

`/tmp/testdaemon.log` — activity log written by the daemon process

## See Also

devman(1), fork(2), setsid(2)
