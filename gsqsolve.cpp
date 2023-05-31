// Solver for the Genius Square puzzle:
//   https://www.happypuzzle.co.uk/family-puzzles-and-games/genius-square
//
// Normal usage is to just list the 7 values directly from the dice rolls, i.e.
//
//   $ ./gsqsolve c4 b1 e5 a6 d2 c5 a5
//
// ...and it will print a little ANSI color image of a solved board
// position.
//
// The seven dice that come with the game have faces chosen so that
// every board position they generate is solvable.  If you specify seven
// "blocker" positions that can't come from the dice it will still
// try to find a solution, but it will print a warning.
//
// The fact that the dice always generate a solution can be verified
// by running:
//
//   $ ./gsqsolve --verify-all
//
// If no errors are detected it will simply exit quietly.
//
// Finally, if you just want to see it solve a random board position:
//
//   $ ./gsqsolve --random
//
// ...or to solve several:
//
//   $ ./gsqsolve --random 10

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cassert>
#include <cstring>
#include <ctime>
#include <array>
#include <sysexits.h>

namespace {

// We encode each board position using a 1-hot value in a bitmask.
// There are 36 board positions, so this fits nicely in a 64-bit register.
// This means that we can also hold a set of positions in a register
// and check for set intersection using bit operations alone.
using board_bitmask_t = std::uint64_t;

[[nodiscard]] static auto constexpr sbit(unsigned row, unsigned col) noexcept -> board_bitmask_t
{
	assert(row < 6);
	assert(col < 6);
	return static_cast<board_bitmask_t>(1) << static_cast<board_bitmask_t>(row * 6 + col);
}

[[nodiscard]] static auto constexpr sbit(char const *id) noexcept -> board_bitmask_t
{
	if (((id[0] >= 'A' and id[0] <= 'F') or (id[0] >= 'a' and id[0] <= 'f')) and
	    id[1] >= '1' and id[1] <= '6' and id[2] == '\0') {
		auto const row = (static_cast<unsigned>(id[0]) - 1) & 7;
		auto const col = static_cast<unsigned>(id[1] - '1');
		return sbit(row, col);
	}
	[[unlikely]] return 0;
}

// Parse a specification of one die from a string listing the six board
// positions, space separated:
[[nodiscard]] static auto consteval parse_dice_values(char const *str) noexcept -> std::array<board_bitmask_t, 6>
{
	std::array<board_bitmask_t, 6> rv;

	for (unsigned i = 0; i < rv.size(); i++) {
		std::array<char, 3> const id = { str[0], str[1], '\0' };
		auto const b = sbit(id.data());
		// assert(b != 0);
		rv[i] = b;
		// assert(str[2] == (i == rv.size() - 1) ? '\0' : ' ');
		str += 3;
	}
	return rv;
}

// Object describing each die
class blocker_die : public std::array<board_bitmask_t, 6> {
    public:
	consteval /* implicit */ blocker_die(char const *str) noexcept
		: std::array<board_bitmask_t, 6>(parse_dice_values(str))
	{
	}

	[[nodiscard]] auto roll() const noexcept -> board_bitmask_t
	{
		// Modern C++ has much better random-number APIs than rand()
		// in <random>, but since the stakes are so low here we might
		// as well just use the terrible old libc function and skip
		// bringing in a lot of extra STL code:
		return (*this)[static_cast<unsigned>(std::rand()) % this->size()];
	}

	// Return a possibly-smaller array containing just the values of the unique faces.
	// This is used by the "--verify-all" code when producing all possible rolls.
	// Caller is responsible for specifying a large-enough destination size
	// via a template parameter.
	template<unsigned DEST_ARRAY_SIZE>
	[[nodiscard]] auto consteval without_dups_sized() const noexcept -> std::array<board_bitmask_t, DEST_ARRAY_SIZE>
	{
		std::array<board_bitmask_t, DEST_ARRAY_SIZE> rv;
		unsigned used = 0;

		rv.fill(0);
		for (auto const v : *this)
			for (unsigned i = 0;; i++) {
				if (i >= used) {
					rv[used++] = v;
					break;
				}
				if (rv[i] == v)
					break;
			}
		return rv;
	}

	// Returns the number of unique faces on the die
	[[nodiscard]] auto consteval num_unique_faces() const noexcept -> unsigned
	{
		auto const uniq = without_dups_sized<6>();
		unsigned i = 0;

		for (auto const v : uniq) {
			if (v == 0)
				break;
			i++;
		}
		return i;
	}
};

// Dice values, per https://www.reddit.com/r/boardgames/comments/kxt1q3/comment/gjc5m2n/
static constexpr std::array<blocker_die, 7> blocker_dice = {
	"A1 C1 D1 D2 E2 F3",
	"A2 B2 C2 A3 B1 B3",
	"C3 D3 E3 B4 C4 D4",
	"E1 F2 F2 B6 A5 A5",
	"A4 B5 C6 C5 D6 F6",
	"E4 F4 E5 F5 D5 E6",
	"F1 F1 F1 A6 A6 A6",
};

// Define a "unique_faces_<n>" variable for each of the seven dice.  We
// go through a lot of effort to size each array at compile-time so that
// the run-time iteration code can be as simple as possible.
#define MAKE_UNIQUE_FACES(n) static constexpr auto unique_faces_##n = blocker_dice[n].without_dups_sized<blocker_dice[n].num_unique_faces()>()
MAKE_UNIQUE_FACES(0);
MAKE_UNIQUE_FACES(1);
MAKE_UNIQUE_FACES(2);
MAKE_UNIQUE_FACES(3);
MAKE_UNIQUE_FACES(4);
MAKE_UNIQUE_FACES(5);
MAKE_UNIQUE_FACES(6);
#undef MAKE_UNIQUE_FACES

// Given a bitmask with (up to) 7 bits set, check that it could have
// actually resulted from a dice roll6
[[nodiscard]] static auto blockers_are_valid_roll(board_bitmask_t blockers)
{
	unsigned saw_die = 0;

	for (unsigned die_num = 0; die_num < blocker_dice.size(); die_num++) {
		for (auto const die_value : blocker_dice[die_num]) {
			assert((die_value & (die_value - 1)) == 0);
			if ((blockers & die_value) != 0) {
				saw_die |= 1u << die_num;
				break;
			}
		}
	}
	return saw_die == 0b1'111'111;
}

// Generate a bitmask of "blocker" pieces by rolling the dice
[[nodiscard]] static auto random_blockers() noexcept -> board_bitmask_t
{
	board_bitmask_t used = 0;

	for (auto const& d : blocker_dice) {
		auto const roll = d.roll();
		assert((used & roll) == 0);
		used += roll;
	}
	assert(blockers_are_valid_roll(used));
	return used;
}

// Shape:
//   XX
//   XX
[[nodiscard]] static auto consteval square2_2_at(unsigned row, unsigned col) noexcept -> board_bitmask_t
{
	return sbit(row, col) | sbit(row + 1, col) | sbit(row, col + 1) | sbit(row + 1, col + 1);
}

static constexpr board_bitmask_t square2_2[] = {
	square2_2_at(0, 0), square2_2_at(0, 1), square2_2_at(0, 2), square2_2_at(0, 3), square2_2_at(0, 4),
	square2_2_at(1, 0), square2_2_at(1, 1), square2_2_at(1, 2), square2_2_at(1, 3), square2_2_at(1, 4),
	square2_2_at(2, 0), square2_2_at(2, 1), square2_2_at(2, 2), square2_2_at(2, 3), square2_2_at(2, 4),
	square2_2_at(3, 0), square2_2_at(3, 1), square2_2_at(3, 2), square2_2_at(3, 3), square2_2_at(3, 4),
	square2_2_at(4, 0), square2_2_at(4, 1), square2_2_at(4, 2), square2_2_at(4, 3), square2_2_at(4, 4),
};

// Shape:
//   XX    X
//         X
[[nodiscard]] static auto consteval line2_h_at(unsigned row, unsigned col) noexcept -> board_bitmask_t
{
	return sbit(row, col) | sbit(row, col + 1);
}
[[nodiscard]] static auto consteval line2_v_at(unsigned row, unsigned col) noexcept -> board_bitmask_t
{
	return sbit(row, col) | sbit(row + 1, col);
}

static constexpr board_bitmask_t line2[] = {
	line2_h_at(0, 0), line2_h_at(0, 1), line2_h_at(0, 2), line2_h_at(0, 3), line2_h_at(0, 4),
	line2_h_at(1, 0), line2_h_at(1, 1), line2_h_at(1, 2), line2_h_at(1, 3), line2_h_at(1, 4),
	line2_h_at(2, 0), line2_h_at(2, 1), line2_h_at(2, 2), line2_h_at(2, 3), line2_h_at(2, 4),
	line2_h_at(3, 0), line2_h_at(3, 1), line2_h_at(3, 2), line2_h_at(3, 3), line2_h_at(3, 4),
	line2_h_at(4, 0), line2_h_at(4, 1), line2_h_at(4, 2), line2_h_at(4, 3), line2_h_at(4, 4),
	line2_h_at(5, 0), line2_h_at(5, 1), line2_h_at(5, 2), line2_h_at(5, 3), line2_h_at(5, 4),

	line2_v_at(0, 0), line2_v_at(0, 1), line2_v_at(0, 2), line2_v_at(0, 3), line2_v_at(0, 4), line2_v_at(0, 5),
	line2_v_at(1, 0), line2_v_at(1, 1), line2_v_at(1, 2), line2_v_at(1, 3), line2_v_at(1, 4), line2_v_at(1, 5),
	line2_v_at(2, 0), line2_v_at(2, 1), line2_v_at(2, 2), line2_v_at(2, 3), line2_v_at(2, 4), line2_v_at(2, 5),
	line2_v_at(3, 0), line2_v_at(3, 1), line2_v_at(3, 2), line2_v_at(3, 3), line2_v_at(3, 4), line2_v_at(3, 5),
	line2_v_at(4, 0), line2_v_at(4, 1), line2_v_at(4, 2), line2_v_at(4, 3), line2_v_at(4, 4), line2_v_at(4, 5),
};

// Shape:
//   XXX    X
//          X
//          X
[[nodiscard]] static auto consteval line3_h_at(unsigned row, unsigned col) noexcept -> board_bitmask_t
{
	return line2_h_at(row, col) | sbit(row, col + 2);
}
[[nodiscard]] static auto consteval line3_v_at(unsigned row, unsigned col) noexcept -> board_bitmask_t
{
	return line2_v_at(row, col) | sbit(row + 2, col);
}

static constexpr board_bitmask_t line3[] = {
	line3_h_at(0, 0), line3_h_at(0, 1), line3_h_at(0, 2), line3_h_at(0, 3),
	line3_h_at(1, 0), line3_h_at(1, 1), line3_h_at(1, 2), line3_h_at(1, 3),
	line3_h_at(2, 0), line3_h_at(2, 1), line3_h_at(2, 2), line3_h_at(2, 3),
	line3_h_at(3, 0), line3_h_at(3, 1), line3_h_at(3, 2), line3_h_at(3, 3),
	line3_h_at(4, 0), line3_h_at(4, 1), line3_h_at(4, 2), line3_h_at(4, 3),
	line3_h_at(5, 0), line3_h_at(5, 1), line3_h_at(5, 2), line3_h_at(5, 3),

	line3_v_at(0, 0), line3_v_at(0, 1), line3_v_at(0, 2), line3_v_at(0, 3), line3_v_at(0, 4), line3_v_at(0, 5),
	line3_v_at(1, 0), line3_v_at(1, 1), line3_v_at(1, 2), line3_v_at(1, 3), line3_v_at(1, 4), line3_v_at(1, 5),
	line3_v_at(2, 0), line3_v_at(2, 1), line3_v_at(2, 2), line3_v_at(2, 3), line3_v_at(2, 4), line3_v_at(2, 5),
	line3_v_at(3, 0), line3_v_at(3, 1), line3_v_at(3, 2), line3_v_at(3, 3), line3_v_at(3, 4), line3_v_at(3, 5),
};

// Shape:
//   XXXX    X
//           X
//           X
//           X
[[nodiscard]] static auto consteval line4_h_at(unsigned row, unsigned col) noexcept -> board_bitmask_t
{
	return line3_h_at(row, col) | sbit(row, col + 3);
}
[[nodiscard]] static auto consteval line4_v_at(unsigned row, unsigned col) noexcept -> board_bitmask_t
{
	return line3_v_at(row, col) | sbit(row + 3, col);
}

static constexpr board_bitmask_t line4[] = {
	line4_h_at(0, 0), line4_h_at(0, 1), line4_h_at(0, 2),
	line4_h_at(1, 0), line4_h_at(1, 1), line4_h_at(1, 2),
	line4_h_at(2, 0), line4_h_at(2, 1), line4_h_at(2, 2),
	line4_h_at(3, 0), line4_h_at(3, 1), line4_h_at(3, 2),
	line4_h_at(4, 0), line4_h_at(4, 1), line4_h_at(4, 2),
	line4_h_at(5, 0), line4_h_at(5, 1), line4_h_at(5, 2),

	line4_v_at(0, 0), line4_v_at(0, 1), line4_v_at(0, 2), line4_v_at(0, 3), line4_v_at(0, 4), line4_v_at(0, 5),
	line4_v_at(1, 0), line4_v_at(1, 1), line4_v_at(1, 2), line4_v_at(1, 3), line4_v_at(1, 4), line4_v_at(1, 5),
	line4_v_at(2, 0), line4_v_at(2, 1), line4_v_at(2, 2), line4_v_at(2, 3), line4_v_at(2, 4), line4_v_at(2, 5),
};

// Shape:
//    X    X     XX    XX
//   XX    XX     X    X
[[nodiscard]] static auto consteval lblock2_ul_at(unsigned row, unsigned col) noexcept -> board_bitmask_t
{
	return line2_h_at(row + 1, col) | sbit(row, col + 1);
}
[[nodiscard]] static auto consteval lblock2_ur_at(unsigned row, unsigned col) noexcept -> board_bitmask_t
{
	return line2_h_at(row + 1, col) | sbit(row, col);
}
[[nodiscard]] static auto consteval lblock2_bl_at(unsigned row, unsigned col) noexcept -> board_bitmask_t
{
	return line2_h_at(row, col) | sbit(row + 1, col + 1);
}
[[nodiscard]] static auto consteval lblock2_br_at(unsigned row, unsigned col) noexcept -> board_bitmask_t
{
	return line2_h_at(row, col) | sbit(row + 1, col);
}

static constexpr board_bitmask_t lblock2[] = {
	lblock2_ul_at(0, 0), lblock2_ul_at(0, 1), lblock2_ul_at(0, 2), lblock2_ul_at(0, 3), lblock2_ul_at(0, 4),
	lblock2_ul_at(1, 0), lblock2_ul_at(1, 1), lblock2_ul_at(1, 2), lblock2_ul_at(1, 3), lblock2_ul_at(1, 4),
	lblock2_ul_at(2, 0), lblock2_ul_at(2, 1), lblock2_ul_at(2, 2), lblock2_ul_at(2, 3), lblock2_ul_at(2, 4),
	lblock2_ul_at(3, 0), lblock2_ul_at(3, 1), lblock2_ul_at(3, 2), lblock2_ul_at(3, 3), lblock2_ul_at(3, 4),
	lblock2_ul_at(4, 0), lblock2_ul_at(4, 1), lblock2_ul_at(4, 2), lblock2_ul_at(4, 3), lblock2_ul_at(4, 4),

	lblock2_ur_at(0, 0), lblock2_ur_at(0, 1), lblock2_ur_at(0, 2), lblock2_ur_at(0, 3), lblock2_ur_at(0, 4),
	lblock2_ur_at(1, 0), lblock2_ur_at(1, 1), lblock2_ur_at(1, 2), lblock2_ur_at(1, 3), lblock2_ur_at(1, 4),
	lblock2_ur_at(2, 0), lblock2_ur_at(2, 1), lblock2_ur_at(2, 2), lblock2_ur_at(2, 3), lblock2_ur_at(2, 4),
	lblock2_ur_at(3, 0), lblock2_ur_at(3, 1), lblock2_ur_at(3, 2), lblock2_ur_at(3, 3), lblock2_ur_at(3, 4),
	lblock2_ur_at(4, 0), lblock2_ur_at(4, 1), lblock2_ur_at(4, 2), lblock2_ur_at(4, 3), lblock2_ur_at(4, 4),

	lblock2_bl_at(0, 0), lblock2_bl_at(0, 1), lblock2_bl_at(0, 2), lblock2_bl_at(0, 3), lblock2_bl_at(0, 4),
	lblock2_bl_at(1, 0), lblock2_bl_at(1, 1), lblock2_bl_at(1, 2), lblock2_bl_at(1, 3), lblock2_bl_at(1, 4),
	lblock2_bl_at(2, 0), lblock2_bl_at(2, 1), lblock2_bl_at(2, 2), lblock2_bl_at(2, 3), lblock2_bl_at(2, 4),
	lblock2_bl_at(3, 0), lblock2_bl_at(3, 1), lblock2_bl_at(3, 2), lblock2_bl_at(3, 3), lblock2_bl_at(3, 4),
	lblock2_bl_at(4, 0), lblock2_bl_at(4, 1), lblock2_bl_at(4, 2), lblock2_bl_at(4, 3), lblock2_bl_at(4, 4),

	lblock2_br_at(0, 0), lblock2_br_at(0, 1), lblock2_br_at(0, 2), lblock2_br_at(0, 3), lblock2_br_at(0, 4),
	lblock2_br_at(1, 0), lblock2_br_at(1, 1), lblock2_br_at(1, 2), lblock2_br_at(1, 3), lblock2_br_at(1, 4),
	lblock2_br_at(2, 0), lblock2_br_at(2, 1), lblock2_br_at(2, 2), lblock2_br_at(2, 3), lblock2_br_at(2, 4),
	lblock2_br_at(3, 0), lblock2_br_at(3, 1), lblock2_br_at(3, 2), lblock2_br_at(3, 3), lblock2_br_at(3, 4),
	lblock2_br_at(4, 0), lblock2_br_at(4, 1), lblock2_br_at(4, 2), lblock2_br_at(4, 3), lblock2_br_at(4, 4),
};

// Shape:
//     X    X      XXX    XXX    X    X    XX   XX
//   XXX    XXX      X    X      X    X     X   X
//                              XX    XX    X   X
[[nodiscard]] static auto consteval lblock3_h_ul_at(unsigned row, unsigned col) noexcept -> board_bitmask_t
{
	return sbit(row, col + 2) | line3_h_at(row + 1, col);
}
[[nodiscard]] static auto consteval lblock3_h_ur_at(unsigned row, unsigned col) noexcept -> board_bitmask_t
{
	return sbit(row, col) | line3_h_at(row + 1, col);
}
[[nodiscard]] static auto consteval lblock3_h_bl_at(unsigned row, unsigned col) noexcept -> board_bitmask_t
{
	return line3_h_at(row, col) | sbit(row + 1, col + 2);
}
[[nodiscard]] static auto consteval lblock3_h_br_at(unsigned row, unsigned col) noexcept -> board_bitmask_t
{
	return line3_h_at(row, col) | sbit(row + 1, col);
}
[[nodiscard]] static auto consteval lblock3_v_ul_at(unsigned row, unsigned col) noexcept -> board_bitmask_t
{
	return sbit(row + 2, col) | line3_v_at(row, col + 1);
}
[[nodiscard]] static auto consteval lblock3_v_ur_at(unsigned row, unsigned col) noexcept -> board_bitmask_t
{
	return line3_v_at(row, col) | sbit(row + 2, col + 1);
}
[[nodiscard]] static auto consteval lblock3_v_bl_at(unsigned row, unsigned col) noexcept -> board_bitmask_t
{
	return sbit(row, col) | line3_v_at(row, col + 1);
}
[[nodiscard]] static auto consteval lblock3_v_br_at(unsigned row, unsigned col) noexcept -> board_bitmask_t
{
	return line3_v_at(row, col) | sbit(row, col + 1);
}

static constexpr board_bitmask_t lblock3[] = {
	lblock3_h_ul_at(0, 0), lblock3_h_ul_at(0, 1), lblock3_h_ul_at(0, 2), lblock3_h_ul_at(0, 3),
	lblock3_h_ul_at(1, 0), lblock3_h_ul_at(1, 1), lblock3_h_ul_at(1, 2), lblock3_h_ul_at(1, 3),
	lblock3_h_ul_at(2, 0), lblock3_h_ul_at(2, 1), lblock3_h_ul_at(2, 2), lblock3_h_ul_at(2, 3),
	lblock3_h_ul_at(3, 0), lblock3_h_ul_at(3, 1), lblock3_h_ul_at(3, 2), lblock3_h_ul_at(3, 3),
	lblock3_h_ul_at(4, 0), lblock3_h_ul_at(4, 1), lblock3_h_ul_at(4, 2), lblock3_h_ul_at(4, 3),

	lblock3_h_ur_at(0, 0), lblock3_h_ur_at(0, 1), lblock3_h_ur_at(0, 2), lblock3_h_ur_at(0, 3),
	lblock3_h_ur_at(1, 0), lblock3_h_ur_at(1, 1), lblock3_h_ur_at(1, 2), lblock3_h_ur_at(1, 3),
	lblock3_h_ur_at(2, 0), lblock3_h_ur_at(2, 1), lblock3_h_ur_at(2, 2), lblock3_h_ur_at(2, 3),
	lblock3_h_ur_at(3, 0), lblock3_h_ur_at(3, 1), lblock3_h_ur_at(3, 2), lblock3_h_ur_at(3, 3),
	lblock3_h_ur_at(4, 0), lblock3_h_ur_at(4, 1), lblock3_h_ur_at(4, 2), lblock3_h_ur_at(4, 3),

	lblock3_h_bl_at(0, 0), lblock3_h_bl_at(0, 1), lblock3_h_bl_at(0, 2), lblock3_h_bl_at(0, 3),
	lblock3_h_bl_at(1, 0), lblock3_h_bl_at(1, 1), lblock3_h_bl_at(1, 2), lblock3_h_bl_at(1, 3),
	lblock3_h_bl_at(2, 0), lblock3_h_bl_at(2, 1), lblock3_h_bl_at(2, 2), lblock3_h_bl_at(2, 3),
	lblock3_h_bl_at(3, 0), lblock3_h_bl_at(3, 1), lblock3_h_bl_at(3, 2), lblock3_h_bl_at(3, 3),
	lblock3_h_bl_at(4, 0), lblock3_h_bl_at(4, 1), lblock3_h_bl_at(4, 2), lblock3_h_bl_at(4, 3),

	lblock3_h_br_at(0, 0), lblock3_h_br_at(0, 1), lblock3_h_br_at(0, 2), lblock3_h_br_at(0, 3),
	lblock3_h_br_at(1, 0), lblock3_h_br_at(1, 1), lblock3_h_br_at(1, 2), lblock3_h_br_at(1, 3),
	lblock3_h_br_at(2, 0), lblock3_h_br_at(2, 1), lblock3_h_br_at(2, 2), lblock3_h_br_at(2, 3),
	lblock3_h_br_at(3, 0), lblock3_h_br_at(3, 1), lblock3_h_br_at(3, 2), lblock3_h_br_at(3, 3),
	lblock3_h_br_at(4, 0), lblock3_h_br_at(4, 1), lblock3_h_br_at(4, 2), lblock3_h_br_at(4, 3),

	lblock3_v_ul_at(0, 0), lblock3_v_ul_at(0, 1), lblock3_v_ul_at(0, 2), lblock3_v_ul_at(0, 3), lblock3_v_ul_at(0, 4),
	lblock3_v_ul_at(1, 0), lblock3_v_ul_at(1, 1), lblock3_v_ul_at(1, 2), lblock3_v_ul_at(1, 3), lblock3_v_ul_at(1, 4),
	lblock3_v_ul_at(2, 0), lblock3_v_ul_at(2, 1), lblock3_v_ul_at(2, 2), lblock3_v_ul_at(2, 3), lblock3_v_ul_at(2, 4),
	lblock3_v_ul_at(3, 0), lblock3_v_ul_at(3, 1), lblock3_v_ul_at(3, 2), lblock3_v_ul_at(3, 3), lblock3_v_ul_at(3, 4),

	lblock3_v_ur_at(0, 0), lblock3_v_ur_at(0, 1), lblock3_v_ur_at(0, 2), lblock3_v_ur_at(0, 3), lblock3_v_ur_at(0, 4),
	lblock3_v_ur_at(1, 0), lblock3_v_ur_at(1, 1), lblock3_v_ur_at(1, 2), lblock3_v_ur_at(1, 3), lblock3_v_ur_at(1, 4),
	lblock3_v_ur_at(2, 0), lblock3_v_ur_at(2, 1), lblock3_v_ur_at(2, 2), lblock3_v_ur_at(2, 3), lblock3_v_ur_at(2, 4),
	lblock3_v_ur_at(3, 0), lblock3_v_ur_at(3, 1), lblock3_v_ur_at(3, 2), lblock3_v_ur_at(3, 3), lblock3_v_ur_at(3, 4),

	lblock3_v_bl_at(0, 0), lblock3_v_bl_at(0, 1), lblock3_v_bl_at(0, 2), lblock3_v_bl_at(0, 3), lblock3_v_bl_at(0, 4),
	lblock3_v_bl_at(1, 0), lblock3_v_bl_at(1, 1), lblock3_v_bl_at(1, 2), lblock3_v_bl_at(1, 3), lblock3_v_bl_at(1, 4),
	lblock3_v_bl_at(2, 0), lblock3_v_bl_at(2, 1), lblock3_v_bl_at(2, 2), lblock3_v_bl_at(2, 3), lblock3_v_bl_at(2, 4),
	lblock3_v_bl_at(3, 0), lblock3_v_bl_at(3, 1), lblock3_v_bl_at(3, 2), lblock3_v_bl_at(3, 3), lblock3_v_bl_at(3, 4),

	lblock3_v_br_at(0, 0), lblock3_v_br_at(0, 1), lblock3_v_br_at(0, 2), lblock3_v_br_at(0, 3), lblock3_v_br_at(0, 4),
	lblock3_v_br_at(1, 0), lblock3_v_br_at(1, 1), lblock3_v_br_at(1, 2), lblock3_v_br_at(1, 3), lblock3_v_br_at(1, 4),
	lblock3_v_br_at(2, 0), lblock3_v_br_at(2, 1), lblock3_v_br_at(2, 2), lblock3_v_br_at(2, 3), lblock3_v_br_at(2, 4),
	lblock3_v_br_at(3, 0), lblock3_v_br_at(3, 1), lblock3_v_br_at(3, 2), lblock3_v_br_at(3, 3), lblock3_v_br_at(3, 4),
};

// Shape:
//   XX     XX    X   X
//    XX   XX    XX   XX
//               X     X
[[nodiscard]] static auto consteval zblock_h_urbl_at(unsigned row, unsigned col) noexcept -> board_bitmask_t
{
	return line2_h_at(row, col) | line2_h_at(row + 1, col + 1);
}
[[nodiscard]] static auto consteval zblock_h_ulbr_at(unsigned row, unsigned col) noexcept -> board_bitmask_t
{
	return line2_h_at(row, col + 1) | line2_h_at(row + 1, col);
}
[[nodiscard]] static auto consteval zblock_v_ulbr_at(unsigned row, unsigned col) noexcept -> board_bitmask_t
{
	return line2_v_at(row + 1, col) | line2_v_at(row, col + 1);
}
[[nodiscard]] static auto consteval zblock_v_urbl_at(unsigned row, unsigned col) noexcept -> board_bitmask_t
{
	return line2_v_at(row, col) | line2_v_at(row + 1, col + 1);
}

static constexpr board_bitmask_t zblock[] = {
	zblock_h_urbl_at(0, 0), zblock_h_urbl_at(0, 1), zblock_h_urbl_at(0, 2), zblock_h_urbl_at(0, 3),
	zblock_h_urbl_at(1, 0), zblock_h_urbl_at(1, 1), zblock_h_urbl_at(1, 2), zblock_h_urbl_at(1, 3),
	zblock_h_urbl_at(2, 0), zblock_h_urbl_at(2, 1), zblock_h_urbl_at(2, 2), zblock_h_urbl_at(2, 3),
	zblock_h_urbl_at(3, 0), zblock_h_urbl_at(3, 1), zblock_h_urbl_at(3, 2), zblock_h_urbl_at(3, 3),
	zblock_h_urbl_at(4, 0), zblock_h_urbl_at(4, 1), zblock_h_urbl_at(4, 2), zblock_h_urbl_at(4, 3),

	zblock_h_ulbr_at(0, 0), zblock_h_ulbr_at(0, 1), zblock_h_ulbr_at(0, 2), zblock_h_ulbr_at(0, 3),
	zblock_h_ulbr_at(1, 0), zblock_h_ulbr_at(1, 1), zblock_h_ulbr_at(1, 2), zblock_h_ulbr_at(1, 3),
	zblock_h_ulbr_at(2, 0), zblock_h_ulbr_at(2, 1), zblock_h_ulbr_at(2, 2), zblock_h_ulbr_at(2, 3),
	zblock_h_ulbr_at(3, 0), zblock_h_ulbr_at(3, 1), zblock_h_ulbr_at(3, 2), zblock_h_ulbr_at(3, 3),
	zblock_h_ulbr_at(4, 0), zblock_h_ulbr_at(4, 1), zblock_h_ulbr_at(4, 2), zblock_h_ulbr_at(4, 3),

	zblock_v_ulbr_at(0, 0), zblock_v_ulbr_at(0, 1), zblock_v_ulbr_at(0, 2), zblock_v_ulbr_at(0, 3), zblock_v_ulbr_at(0, 4),
	zblock_v_ulbr_at(1, 0), zblock_v_ulbr_at(1, 1), zblock_v_ulbr_at(1, 2), zblock_v_ulbr_at(1, 3), zblock_v_ulbr_at(1, 4),
	zblock_v_ulbr_at(2, 0), zblock_v_ulbr_at(2, 1), zblock_v_ulbr_at(2, 2), zblock_v_ulbr_at(2, 3), zblock_v_ulbr_at(2, 4),
	zblock_v_ulbr_at(3, 0), zblock_v_ulbr_at(3, 1), zblock_v_ulbr_at(3, 2), zblock_v_ulbr_at(3, 3), zblock_v_ulbr_at(3, 4),

	zblock_v_urbl_at(0, 0), zblock_v_urbl_at(0, 1), zblock_v_urbl_at(0, 2), zblock_v_urbl_at(0, 3), zblock_v_urbl_at(0, 4),
	zblock_v_urbl_at(1, 0), zblock_v_urbl_at(1, 1), zblock_v_urbl_at(1, 2), zblock_v_urbl_at(1, 3), zblock_v_urbl_at(1, 4),
	zblock_v_urbl_at(2, 0), zblock_v_urbl_at(2, 1), zblock_v_urbl_at(2, 2), zblock_v_urbl_at(2, 3), zblock_v_urbl_at(2, 4),
	zblock_v_urbl_at(3, 0), zblock_v_urbl_at(3, 1), zblock_v_urbl_at(3, 2), zblock_v_urbl_at(3, 3), zblock_v_urbl_at(3, 4),
};

// Shape:
//   XXX    X    X     X
//    X    XXX   XX   XX
//               X     X
[[nodiscard]] static auto consteval tblock_h_l_at(unsigned row, unsigned col) noexcept -> board_bitmask_t
{
	return line3_h_at(row, col) | sbit(row + 1, col + 1);
}
[[nodiscard]] static auto consteval tblock_h_u_at(unsigned row, unsigned col) noexcept -> board_bitmask_t
{
	return line3_h_at(row + 1, col) | sbit(row, col + 1);
}
[[nodiscard]] static auto consteval tblock_v_r_at(unsigned row, unsigned col) noexcept -> board_bitmask_t
{
	return line3_v_at(row, col) | sbit(row + 1, col + 1);
}
[[nodiscard]] static auto consteval tblock_v_l_at(unsigned row, unsigned col) noexcept -> board_bitmask_t
{
	return line3_v_at(row, col + 1) | sbit(row + 1, col);
}

static constexpr board_bitmask_t tblock[] = {
	tblock_h_l_at(0, 0), tblock_h_l_at(0, 1), tblock_h_l_at(0, 2), tblock_h_l_at(0, 3),
	tblock_h_l_at(1, 0), tblock_h_l_at(1, 1), tblock_h_l_at(1, 2), tblock_h_l_at(1, 3),
	tblock_h_l_at(2, 0), tblock_h_l_at(2, 1), tblock_h_l_at(2, 2), tblock_h_l_at(2, 3),
	tblock_h_l_at(3, 0), tblock_h_l_at(3, 1), tblock_h_l_at(3, 2), tblock_h_l_at(3, 3),
	tblock_h_l_at(4, 0), tblock_h_l_at(4, 1), tblock_h_l_at(4, 2), tblock_h_l_at(4, 3),

	tblock_h_u_at(0, 0), tblock_h_u_at(0, 1), tblock_h_u_at(0, 2), tblock_h_u_at(0, 3),
	tblock_h_u_at(1, 0), tblock_h_u_at(1, 1), tblock_h_u_at(1, 2), tblock_h_u_at(1, 3),
	tblock_h_u_at(2, 0), tblock_h_u_at(2, 1), tblock_h_u_at(2, 2), tblock_h_u_at(2, 3),
	tblock_h_u_at(3, 0), tblock_h_u_at(3, 1), tblock_h_u_at(3, 2), tblock_h_u_at(3, 3),
	tblock_h_u_at(4, 0), tblock_h_u_at(4, 1), tblock_h_u_at(4, 2), tblock_h_u_at(4, 3),

	tblock_v_r_at(0, 0), tblock_v_r_at(0, 1), tblock_v_r_at(0, 2), tblock_v_r_at(0, 3), tblock_v_r_at(0, 4),
	tblock_v_r_at(1, 0), tblock_v_r_at(1, 1), tblock_v_r_at(1, 2), tblock_v_r_at(1, 3), tblock_v_r_at(1, 4),
	tblock_v_r_at(2, 0), tblock_v_r_at(2, 1), tblock_v_r_at(2, 2), tblock_v_r_at(2, 3), tblock_v_r_at(2, 4),
	tblock_v_r_at(3, 0), tblock_v_r_at(3, 1), tblock_v_r_at(3, 2), tblock_v_r_at(3, 3), tblock_v_r_at(3, 4),

	tblock_v_l_at(0, 0), tblock_v_l_at(0, 1), tblock_v_l_at(0, 2), tblock_v_l_at(0, 3), tblock_v_l_at(0, 4),
	tblock_v_l_at(1, 0), tblock_v_l_at(1, 1), tblock_v_l_at(1, 2), tblock_v_l_at(1, 3), tblock_v_l_at(1, 4),
	tblock_v_l_at(2, 0), tblock_v_l_at(2, 1), tblock_v_l_at(2, 2), tblock_v_l_at(2, 3), tblock_v_l_at(2, 4),
	tblock_v_l_at(3, 0), tblock_v_l_at(3, 1), tblock_v_l_at(3, 2), tblock_v_l_at(3, 3), tblock_v_l_at(3, 4),
};

enum class piece_id {
	single_block,	// Dark Blue (104)
	line2,		// Brown (101, actually a brighter red)
	line3,		// Orange (43, actually dim yellow)
	line4,		// Grey (100)
	square2_2,	// Green (102)
	lblock2,	// Purple (105)
	lblock3,	// Light Blue (106)
	zblock,		// Red (41)
	tblock,		// Yellow (103)
	blockers,	// White round piece
};

static constexpr std::array<char const *, 10> piece_rendering = {
#if 1
#  define RENDER_BLOCK(color_num)	"\033[" #color_num "m \033[0m"
	RENDER_BLOCK(104),
	RENDER_BLOCK(101),
	RENDER_BLOCK(43),
	RENDER_BLOCK(100),
	RENDER_BLOCK(102),
	RENDER_BLOCK(105),
	RENDER_BLOCK(106),
	RENDER_BLOCK(41),
	RENDER_BLOCK(103),
#  undef RENDER_BLOCK
#else
	"1", "2", "3", "4", "5", "6", "7", "8", "9",
#endif
	"\xE2\x97\x8F",
};

class board {
    public:
	explicit board(board_bitmask_t blockers) noexcept
		: blockers_(blockers)
		// All of the other members are only set in solve()
	{
	}

	// Try to fill the rest of the values to valid piece locations.
	//
	// Returns false if none was found.
	[[nodiscard]] auto solve() noexcept -> bool;

	// Print out the board in ANSI color
	auto print() const noexcept -> void;

    private:
	// These are the 7 "blocker" spaces that the board starts with.
	// This value gets set in the constructor.
	board_bitmask_t const blockers_;
	// The first blocks we place are the ones that take up four
	// spots.  This way we get as many blocks used up as quickly
	// as possible, making it more likely we can find a conflict early.
	//
	// Or to thing of it another way: because we "place" a block with
	// a single bitwise-OR operation, a 4-spot block is just as fast
	// to place as a 2-spot one.  We might as well start with the placements
	// that accomplish the most.
	board_bitmask_t line4_;
	board_bitmask_t square2_2_;
	board_bitmask_t lblock3_;
	board_bitmask_t zblock_;
	board_bitmask_t tblock_;
	// Then the two that take 3 spots:
	board_bitmask_t line3_;
	board_bitmask_t lblock2_;
	// Finally the one that takes two spot:
	board_bitmask_t line2_;
	// There is also the single-square block, but we don't place that.
	// Since it can go anywhere, it's sufficient to find places for
	// all of the other blocks.  The one remaining empty block is then
	// implicitly where the single-square must go.

	// Given a location at the board, which piece got placed there
	auto piece_at(unsigned row, unsigned col) const noexcept -> piece_id;

#ifndef NDEBUG
	auto assert_consistent_() const noexcept -> void;
#endif // !NDEBUG
};

auto board::solve() noexcept -> bool
{
	board_bitmask_t used = this->blockers_;

#define SHAPE_LOOP_START(shape)	\
	for (auto const t_##shape : shape) {		\
		if ((t_##shape & used) == 0) {		\
			this->shape##_ = t_##shape;	\
			used += t_##shape

#define SHAPE_LOOP_END(shape)				\
			used -= t_##shape;		\
		}					\
	}						\
	do { } while (0)

	SHAPE_LOOP_START(line4);
	SHAPE_LOOP_START(square2_2);
	SHAPE_LOOP_START(lblock3);
	SHAPE_LOOP_START(zblock);
	SHAPE_LOOP_START(tblock);
	SHAPE_LOOP_START(line3);
	SHAPE_LOOP_START(lblock2);

	for (auto const t_line2 : line2) {
		if ((t_line2 & used) == 0) {
			this->line2_ = t_line2;
#ifndef NDEBUG
			assert_consistent_();
#endif // !NDEBUG
			return true;
		}
	}

	SHAPE_LOOP_END(lblock2);
	SHAPE_LOOP_END(line3);
	SHAPE_LOOP_END(tblock);
	SHAPE_LOOP_END(zblock);
	SHAPE_LOOP_END(lblock3);
	SHAPE_LOOP_END(square2_2);
	SHAPE_LOOP_END(line4);

#undef SHAPE_LOOP_START
#undef SHAPE_LOOP_END

	[[unlikely]] return false;
}

#ifndef NDEBUG
auto board::assert_consistent_() const noexcept -> void
{
	auto const all_bits = this->blockers_ | this->line4_ | this->square2_2_ | this->lblock3_ | this->zblock_ | this->tblock_ | this->line3_ | this->lblock2_ | this->line2_;
	// Cheap check to make sure no blocks got placed on top each other:
	assert(all_bits ==
	  (this->blockers_ + this->line4_ + this->square2_2_ + this->lblock3_ + this->zblock_ + this->tblock_ + this->line3_ + this->lblock2_ + this->line2_));
	// Verify that there is exactly one unplaced spot (i.e.
	// where the single square will go)
	auto const unused_bits = 0xF'FFFF'FFFFull ^ all_bits;
	assert(unused_bits != 0);
	assert((unused_bits & (unused_bits - 1)) == 0);
}
#endif // !NDEBUG

auto board::piece_at(unsigned row, unsigned col) const noexcept -> piece_id
{
	auto const p = sbit(row, col);

#define SHAPE_CHECK(shape)			\
	if ((p & this->shape##_) != 0)		\
		return piece_id::shape;		\
	do { } while (0)

	SHAPE_CHECK(blockers);
	SHAPE_CHECK(line4);
	SHAPE_CHECK(square2_2);
	SHAPE_CHECK(lblock3);
	SHAPE_CHECK(zblock);
	SHAPE_CHECK(tblock);
	SHAPE_CHECK(line3);
	SHAPE_CHECK(lblock2);
	SHAPE_CHECK(line2);
#undef SHAPE_CHECK

	return piece_id::single_block;
}

auto board::print() const noexcept -> void
{
	for (unsigned row = 0; row < 6; row++) {
		for (unsigned col = 0; col < 6; col++) {
			auto const p = static_cast<unsigned>(piece_at(row, col));
			assert(p < piece_rendering.size());
			fputs(piece_rendering[p], stdout);
		}
		putchar('\n');
	}
}

[[nodiscard]] static auto verify_roll(board_bitmask_t blockers) noexcept -> bool
{
	assert(blockers_are_valid_roll(blockers));
	board b(blockers);
	if (not b.solve()) {
		[[unlikely]] fprintf(stderr, "Error: Couldn't solve board %09llX\n", blockers);
		return false;
	}
	return true;
}

[[nodiscard]] static auto verify_all_possible_rolls() noexcept -> bool
{
	// Iterate through all combinations of *unique* faces on each
	// die.  Since some dice have the same value on multiple faces
	// this reduces the search space a lot:
	bool ok = true;
	for (auto const d0 : unique_faces_0)
		for (auto const d1 : unique_faces_1)
			for (auto const d2 : unique_faces_2)
				for (auto const d3 : unique_faces_3)
					for (auto const d4 : unique_faces_4)
						for (auto const d5 : unique_faces_5)
							for (auto const d6 : unique_faces_6)
								if (not verify_roll(d0 | d1 | d2 | d3 | d4 | d5 | d6))
									[[unlikely]] ok = false;
	return ok;
}

static auto usage(FILE *fp) noexcept -> void
{
	fputs(	"Usage:\n"
		"\t"	"gsqsolve <die_1> <die_2> ... <die_7>\n"
		"\t"	"sqsolve --random [count]\n"
		"\t"	"gsqsolve --verify-all\n", fp);
}

} // anonymous namespace

auto main(int argn, char const * const *argv) noexcept -> int
{
	if (argn == 2) {
		auto const arg = argv[1];
		if (0 == strcmp(arg, "--help")) {
			usage(stdout);
			return EX_OK;
		}
		if (0 == strcmp(arg, "--verify-all"))
			return verify_all_possible_rolls() ? EX_OK : 1;
	}
	if (argn >= 2 and argn <= 3 and 0 == strcmp(argv[1], "--random")) {
		std::srand(static_cast<unsigned>(std::time(nullptr)));

		auto const count = (argn == 2) ? 1u : static_cast<unsigned>(atoi(argv[2]));
		if (count == 0) {
			[[unlikely]] usage(stderr);
			return EX_USAGE;
		}
		for (unsigned i = 0;;) {
			board b(random_blockers());
			if (not b.solve()) {
				[[unlikely]] fputs("Error: No solution!\n", stderr);	// should be impossible!
				return EX_SOFTWARE;
			}
			b.print();
			if (++i >= count) {
				assert(i == count);
				break;
			}
			putchar('\n');
		}
		return EX_OK;
	}
	if (argn != 8) {
		[[unlikely]] usage(stderr);
		return EX_USAGE;
	}
	board_bitmask_t blockers = 0;
	bool parsed_ok = true;
	for (unsigned i = 1; i <= 7; i++) {
		auto const arg = argv[i];
		auto const b = sbit(arg);
		if (b == 0) {
			[[unlikely]] parsed_ok = false;
			fprintf(stderr, "Error: Bad board position: \"%s\"\n", arg);
		}
		if ((blockers & b) != 0) {
			[[unlikely]] parsed_ok = false;
			fprintf(stderr, "Error: Board position listed multiple times: \"%s\"\n", arg);
		}
		blockers |= b;
	}
	if (not parsed_ok) {
		[[unlikely]] usage(stderr);
		return EX_USAGE;
	}
	// We can try to solve any board with 7 blockers, but at least
	// print a warning if this isn't one that is reachable using the
	// game's standard dice, since then we may not have a solution:
	auto const valid_roll = blockers_are_valid_roll(blockers);
	if (not valid_roll)
		[[unlikely]] fputs("Warning: given board is not a valid dice roll\n", stderr);
	board b(blockers);
	if (not b.solve()) {
		[[unlikely]] puts("No solution.");
		assert(not valid_roll);
		return 1;
	}
	b.print();
	return EX_OK;
}
