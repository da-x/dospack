# Dospack - A runner for Dangerous Dave with 'Save game'

Back in 2011, I have written this very-stripped down version of DOSBox in order to have a save-game functionality in the DOSBox emulator. Among other things, I have tried to design a deterministic engine that would be able to reach a reproducible state given a recorded list of input events. Much of the logic is brought from reading [DOSBox](https://www.dosbox.com/)'s code.

As a test, I took the MS-DOS version of Dangerous Dave in order to see that I can get this functionality incorporated.

Because I have lost interest in this development about a month into the process, it does not have much functionality, except from running that game. The upside is that it's the probably the smallest emulator that can run the game, and therefore probably interesting for educational purposes.

## Building

Building is supported under Linux provided that `libsdl` (or `SDL-devel`) is installed.

```
# Copy DAVE.EXE from somewhere
cp DAVE.EXE games/dave

# Build it
make dep
make
```

## Playing Dangerous Dave with save

Save a dump of the VM's memory to `saves/` every 15 seconds

```
./dospack -P saves/ 15000000
```

Load a save:

```
./dospack -M saves/timetrack-000000030000000.dp
```

Run with saved events:

```
./dospack -R events.dat
```

Run with events replay:

```
./dospack -r events.dat
```

## Limitations

* No sound
* Lack of sufficient documentation
* Only runs Dangerous Dave :)
