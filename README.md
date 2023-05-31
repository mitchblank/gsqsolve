gsqsolve: solver for the [Genius Square puzzle](https://www.happypuzzle.co.uk/family-puzzles-and-games/genius-square)

Normal usage is to just list the 7 values directly from the dice rolls, i.e.
```
$ ./gsqsolve c4 b1 e5 a6 d2 c5 a5
```
...and it will print a little ANSI color image of a solved board
position.

The seven dice that come with the game have faces chosen so that
every board position they generate is solvable.  If you specify seven
"blocker" positions that can't come from the dice it will still
try to find a solution, but it will print a warning.

The fact that the dice always generate a solution can be verified
by running:
```
$ ./gsqsolve --verify-all
```
If no errors are detected it will simply exit quietly.

It's also possible to iterate all possible dice values and count how
many solutions each of them has:
```
$ ./gsqsolve --solution-counts | sort -n | less
```

Finally, if you just want to see it solve a random board position:
```
$ ./gsqsolve --random
```
...or to solve several:
```
$ ./gsqsolve --random 10
```
