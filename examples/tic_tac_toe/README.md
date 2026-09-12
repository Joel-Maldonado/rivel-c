# Tic-tac-toe in Rivel

A complete terminal game with a perfect-play computer opponent, local
two-player mode, hints, undo, rematches, and a session scoreboard. Gameplay,
input handling, AI search, and the strategy self-test are written in Rivel.

## Play

From the repository root:

```sh
make
bin/rivelc run examples/tic_tac_toe/main.rivel
```

You play X and move first. Enter a number using the board as a guide:

```text
========================================
          R I V E L  /  X & O
========================================
You: X / Computer: O / You go first

    +---+---+---+
    | 1 | 2 | 3 |
    +---+---+---+
    | 4 | 5 | 6 |
    +---+---+---+
    | 7 | 8 | 9 |
    +---+---+---+

X move [1-9, hint, undo, help, quit] >
```

| Command | Action |
| --- | --- |
| `1` through `9` | Play in an empty square |
| `hint` | Ask the same search engine for your best move |
| `undo` | Undo your move and the computer's reply, or one move in two-player mode |
| `help` | Show the commands |
| `quit` or `q` | End the session |

X always goes first. A completed round updates the scoreboard; answer `y`
to start another round with a fresh board. Scores last for the current
session. Closing stdin also exits cleanly.

## Other modes

```sh
# Share the keyboard with a friend.
bin/rivelc run examples/tic_tac_toe/main.rivel --two-player

# Play O and let the computer open.
bin/rivelc run examples/tic_tac_toe/main.rivel --ai-first

# Watch two perfect players finish a game without any input.
bin/rivelc run examples/tic_tac_toe/main.rivel --demo

# Compile once and keep the native executable.
bin/rivelc -o /tmp/rivel-tic-tac-toe examples/tic_tac_toe/main.rivel
/tmp/rivel-tic-tac-toe --help
```

## How it works

`Board` is a struct with methods for placing pieces, undoing moves, finding
winners, and drawing the board. Lists store the nine cells and move history.
Safe integer parsing and optional narrowing handle input without panicking.

The computer searches the remaining game tree with negamax and alpha-beta
pruning. It prefers quick wins, delays unavoidable losses, and picks the
center or a corner when equally good choices remain. Hints use the same
search for the current player. With perfect play on both sides, the game
ends in a draw.

## Verify

```sh
bin/rivelc run examples/tic_tac_toe/main.rivel --self-test
bash examples/tic_tac_toe/test.sh
```

The Rivel self-test checks every winning line for both players, rejected
moves, board restoration, a forced win, a forced block, and a full-board
draw. It then explores every legal opponent continuation against the
computer's chosen strategy, as both X and O, asserting that the computer
never loses.

The integration script compiles into a temporary directory and checks real
interactive transcripts: wins, draws, rematches, scores, invalid input,
undo, hints, computer play, CLI errors, quit, and end-of-input. Run
`make test-examples` to verify this game and the smaller examples together.
