# Toolkit examples

Four applications, smallest first. Each is one C file that defines a
`tiku_desk_app_descriptor_t` and nothing else, so the same source runs two
ways: as its own process over the window session, or hosted inside a
desktop that linked it.

| example | what it shows |
|---|---|
| `hello.c` | the least an application can be: open a window, paint once |
| `clock.c` | `tick`, and repainting only when the displayed value changed |
| `counter.c` | pointer hit-testing, keys and menu picks reaching one value |
| `sketch.c` | a drag followed from press through move to release |

## Building and running

```sh
cd applications/desktop
make examples                 # build/tiku-example-{hello,clock,counter,sketch}

# in one terminal: a desktop to host them
cd ../tracker && ./build/desktop ~/Desktop

# in another: any example, as its own process
applications/desktop/build/tiku-example-sketch
```

The application finds the desktop at `$HOME/.config/tracker/desk.sock`, and
retries for a few seconds, so either order of starting works. Escape closes
any of them; so does Quit in the window's own menu, which appears in the
global bar when `GlobalMenuBar` is on.

## The two deployments

An application never learns which one it is in. It calls `open`, `frame`,
`menus` and `close` on the services it was handed; out of process those are
backed by the session socket, and linked in they are backed by the
workspace directly.

```c
const tiku_desk_app_descriptor_t tiku_example_hello = {
    .id = "org.tikuos.example.hello",
    .name = "Hello",
    .start = hello_start,       /* open windows, publish menus */
    .stop  = hello_stop,        /* give back what start took   */
    .event = hello_event        /* keys and the pointer        */
};

int main(void) { return tiku_desk_client_run(&tiku_example_hello); }
```

`main` is compiled out by `-DTIKU_EXAMPLE_EMBED`, which leaves the
descriptor for a desktop to host in its own process. That switch is the
whole difference between the two deployments: a device links the
descriptor, a host runs it beside the desktop, and the code is the same.

## What an application owns

Its pixels. `tiku_desk_surface_new` gives it a surface, it draws whatever
it likes, and `frame` hands a finished picture over. Nothing draws into
another process's window, which is why a crashed application leaves a
stale frame rather than a broken desktop.

Coordinates in events are relative to the window's **content**, so a hit
test written once keeps working wherever the window is moved.

Menus travel as plain data: fill in a `tiku_desk_menuset_t`, call `menus`,
and picks come back through `pick` as the command numbers the application
chose. Republish whenever the state behind a row changes -- `counter.c`
disables *Decrease* at zero, and `clock.c` re-marks *Show seconds* -- since
the published set is a snapshot, not a live view.

## Applications, next door

`apps/` holds two written the same way, meant to be used rather than read:
`tiku-edit`, a text editor that takes a file on the command line, and
`tiku-term`, a terminal running a shell on a pty. The desktop offers both
in the Deskbar's leaf menu when it finds them beside itself, so they can
be started without a command line at all.

## Adding one

Copy the closest example into `examples/`, add its name to
`EXAMPLE_NAMES` in the Makefile, and it builds with the rest. `make all`
builds every example, so an application that stops compiling against a
changed toolkit is caught by an ordinary build rather than by someone
trying it months later.
