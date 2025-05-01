// To do: saving and loading saves.

// (Is the right metric cards played, turns,...?)
// Probably collect and return a few metrics: cards, turns, drinks,...
// play_all_games should return stats on each of these
// Return as a struct to main, or whoever, for further analysis.

// To run: g++ -O3 -I"C:\Program Files\boost\boost_1_82_0" bmn_random_parallel.cpp -o bmn
// to compile then ./bmn to run.

// Parallelisation brings playing 1,000,000 standard deck games
// from ~10.5 (bmn3) to ~6.7 seconds.
// Compiling with a -O3 flag brings it down to 0.585 seconds.

#include <iostream>
#include <vector>
#include <algorithm>
#include <random>
#include <array>
#include <boost/circular_buffer.hpp>
#include <map>
#include <unordered_map>
#include <limits>
#include <chrono>
#include <thread> // I want to play games in parallel
#include <mutex>

using std::array;

// Define a new type alias for circular_buffer<int>
using pile = boost::circular_buffer<int>;

// Print the pile
void print_pile(pile &deck){
	array<const char,5> card_values = {'0','J','Q','K','A'};
    for (int card : deck){
		std::cout << card_values[card];
	}
    std::cout << std::endl;
}

void print_game_lengths(std::vector<long> &lengths){
	std::cout << "Number of games by length: ";
	for (long i : lengths){
		std::cout << i << " ";
	}
	std::cout << std::endl;
}

void print_long_games(std::map<long,long> &long_games){
	std::cout << "Number of long (finite) games by size: ";
	for (auto game : long_games){
		std::cout << game.first << ":" << game.second << ",";
	}
	std::cout << std::endl;
}

void print_loops(std::map<long,long> &loop_sizes){
	std::cout << "Number of loops by size: ";
	for (auto loop : loop_sizes){
		std::cout << loop.first << ":" << loop.second << ",";
	}
	std::cout << std::endl;
}

void print_results(std::vector<long>& lengths,
	std::map<long,long>& long_games,
	std::map<long,long>& loop_sizes,
	long& longest,
	pile& longest_game){
	lengths.pop_back(); // I don't really want to include the number of loops
	// in lengths. Note I was already losing games of length > bound.
	while (!lengths.back()){
		lengths.pop_back();
	}
	std::cout << "The longest game: ";
	print_pile(longest_game);
	std::cout << "Length: " << longest << std::endl;
	print_game_lengths(lengths);
	if (long_games.size()>0){
		print_long_games(long_games);
	}
	if (loop_sizes.size()>0){
		print_loops(loop_sizes);
	}
}

// Count the number of cards in the deck
const int count_deck(const array<const int,5> &deck_type){
	int deck_size = 0;
	for (int i : deck_type){
		deck_size += i;
	}
	return deck_size;
}

// Function to generate the initial sorted deck
pile first_deck(const array<const int,5> &deck_type, const int &deck_size) {
    pile deck(deck_size);
    int current = 0;
    for (int i : deck_type) {
        for (int j=0; j<i; j++){
			deck.push_back(current);
		}
		++current;
    }
    return deck;
}

// Function to generate the next permutation of the deck
void next_deck(pile &deck){
	if (!std::next_permutation(deck.begin(), deck.end())){
		deck = pile(0);
	}
}

// Shuffle to get a random deck - pass deck_size as parameter
void random_deck(pile &deck){
	auto rd = std::random_device {}; 
	auto rng = std::default_random_engine { rd() };
	// std::shuffle(deck.begin(), deck.end(), rng); // Replace with Fisher-Yates
	for (int i = deck.size() - 1; i > 0; --i) {
        std::swap(deck[i], deck[rng() % (i + 1)]);
    }
}

// Deal the deck to the players; just two players for now
void deal(pile &deck, array<pile*,2> hands){
	bool turn = false;
	for (int card : deck){
		(*hands[turn]).push_back(card);
		turn = !turn;
	}
}

// Undeal the cards from the two hands, concatenated, to get the starting deck back
// I'm not expecting to use this one much, so speed isn't an issue here.
// It's just for convenience when I want to input a specific starting game.
// (What happens if the deck_size is odd? Check C++ integer division.)
pile undeal(pile &deck){
	const int deck_size = deck.size();
	pile deck2(deck_size);
	for (int i=0; i<deck_size; i++){
		if (!(i%2)){
			deck2.push_back(deck[i/2]);
		}
		else {
			deck2.push_back(deck[deck_size/2 + (i-1)/2]);
		}
	}
	// print_pile(deck);
	// print_pile(deck2);
	return deck2;
}

// Play a turn, i.e. from putting down the first card until cards are picked up
// true: hand1 plays, false: hand2 plays

void play_turn(array<pile*,2> hands, pile &played, bool &turn, const int &deck_size){
	int counter = 0;
	// 1. Before a picture card goes down
	while (!counter && !(*hands[turn]).empty()){
		counter = (*hands[turn]).front();
		(*hands[turn]).pop_front();
		played.push_back(counter);
		// print_pile(*hands[0]);
		// print_pile(*hands[1]);
		turn = !turn;
	}
	// 2. After a picture card goes down
	while (counter && !(*hands[turn]).empty()){
		played.push_back((*hands[turn]).front());
		(*hands[turn]).pop_front();
		// print_pile(*hands[0]);
		// print_pile(*hands[1]);
		if (played.back()){
			counter = played.back();
			turn = !turn;
		}
		else {
			--counter;
		}
	}
	// 3. Winner picks up the cards
	turn = !turn;
	for (int card:played){
		(*hands[turn]).push_back(card);
	}
	played.clear();
}

// Function to play a game of Beggar My Neighbour and return the number of moves
long bmn_game(pile &deck, const int &deck_size, const long &bound){
	pile hand1(deck_size), hand2(deck_size), played(deck_size);
	array<pile*,2> hands = {&hand1, &hand2}; // so active becomes *hands[turn]
	// NB you're not allowed an array of references, so I use an array of pointers
	// (Does that still apply for C++ arrays?)
	deal(deck, hands);
	bool turn = false; // false: hand1 plays, true: hand2 plays
	long turns=0;
	while (!(*hands[turn]).full()){
		play_turn(hands,played,turn,deck_size);
		++turns;
		if (turns == bound){ // Fix this to handle infinite loops
			return turns;
		}
		// print_pile(hand1);
		// print_pile(hand2);
	}
	return turns;
}

// Return a pile of concatenated active+{-1}+inactive cards, for the game history
pile marked_concat(array<pile*,2> hands, bool &turn, const int &deck_size){
	pile this_hand(deck_size+1);
	for (int i : *hands[turn]){
		this_hand.push_back(i);
	}
	this_hand.push_back(-1);
	for (int i : *hands[!turn]){
		this_hand.push_back(i);
	}
	return this_hand;
}

// To hash the marked_concats to search the history in play_bound_game
struct PileHash {
    std::size_t operator()(const pile& p) const {
        std::size_t h = 0;
        // Combine the hash of each element in the pile.
        for (int i : p) {
            // A simple hash combination: you may want something more robust.
            h = h * 31 + std::hash<int>()(i);
        }
        return h;
    }
};

// Play games whose length is >bound to check for loops.
// In general I should set bound so that most games passed to this have loops.
// I want to return the loop length, hands to reach the loop, starting deck,...
// I expect few long games, so speed is less important here.
array<long,2> play_bound_game(pile &deck, const int &deck_size){
	pile hand1(deck_size), hand2(deck_size), played(deck_size);
	array<pile*,2> hands = {&hand1, &hand2}; // so active becomes *hands[turn]
	deal(deck, hands);
	
	bool turn = false; // false: hand1 plays, true: hand2 plays
	long turns=0;
	
	// After each turn, store the current hands in seen.
	// Store as a single pile for simplicity, as a concatenation of
	// active+{-1}+inactive
	// so e.g. if the hands switch places, I will detect that as a loop.
	// (Is that the behaviour I want? hand1+{-1}+hand2 would work too,
	// if I track turn as well.)
	std::unordered_map<pile,long, PileHash> seen;
	pile this_hand = marked_concat(hands,turn,deck_size);
	seen[this_hand] = turns;
	
	while (!(*hands[turn]).full()){
		play_turn(hands,played,turn,deck_size);
		++turns;
		this_hand = marked_concat(hands,turn,deck_size);
		// if this_hand is in seen:
		// for (int i=0; i<seen.size(); i++){
			// if (this_hand == seen[i]){
			// array<long,2> stats = {i,turns-i};
				// return stats;
			// }
		// }
		if (seen.count(this_hand) > 0){
			return {seen[this_hand], turns-seen[this_hand]};
		}
		// if not:
		// seen.push_back(this_hand);
		seen[this_hand] = turns;
	}
	return {turns,0}; // returns (length,0) if it finds a finite game;
	// (length-to-loop, loop length) if not - which is unambiguous.
}

// Each worker thread should play their share of games
void workerThread(
	std::mutex& statsMutex,
	const array<const int,5> &deck_type,
	const int deck_size,
	const long game_count,
	const long bound,
	std::vector<long>& lengths,
	std::map<long, long>& long_games,
	std::map<long, long>& loop_sizes,
	long& longest,
	pile& longest_game
	){
	std::vector<long> my_lengths(bound+2); // these variables are thread-specific
	std::map<long, long> my_long_games;
	std::map<long, long> my_loop_sizes;
	long my_longest=0;
	pile my_longest_game(52);
	
	pile deck=first_deck(deck_type, deck_size);
	
	for (long i=0; i<game_count; ++i) {
		random_deck(deck);
		long turns = bmn_game(deck, deck_size, bound+1);
		if (turns > bound){
			array<long,2> stats = play_bound_game(deck,deck_size);
			// Update long game statistics
			if (stats[1]){ // Found a loop!
				// std::cout << "Found a loop!" << std::endl;
				my_loop_sizes[stats[1]]++;
				my_longest = std::numeric_limits<long>::max();
				my_longest_game = deck;
			}
			else { // Found a long finite game!
				// std::cout << "Found a long game!" << std::endl;
				my_long_games[stats[0]]++;
				if (stats[0]>my_longest){
					my_longest = stats[0];
					my_longest_game = deck;
				}
			}
		}
		// Update statistics
		++my_lengths[turns];
		if (turns > my_longest){
			my_longest = turns;
			my_longest_game = deck;
		}
	}
	
	// Update shared statistics
	{std::lock_guard<std::mutex> lock(statsMutex);
		for (long i=0; i<bound+2; ++i){
			lengths[i] += my_lengths[i];
		}
		for (auto game : my_long_games){
			long_games[game.first] += game.second;
		}
		for (auto loop : my_loop_sizes){
			loop_sizes[loop.first] += loop.second;
		}
		if (my_longest > longest){
			longest = my_longest;
			longest_game = my_longest_game;
		}
	}
}

// Function to play some random games or all games of a given deck_type
// Pass with one argument, or with game_count=0, to play all games of type deck_type
void play_all_games_parallel(const array<const int,5> &deck_type={36,4,4,4,4},
	const long &game_count=1000000,
	const long &bound=1000){
	// Initialise
	std::mutex statsMutex;
	std::vector<long> lengths(bound+2);
	std::map<long, long> long_games;
	std::map<long, long> loop_sizes;
	long longest=0;
	pile longest_game(52);
	
	const int deck_size = count_deck(deck_type);
		
	// Launch worker threads.
	const int numThreads = std::thread::hardware_concurrency();
	std::cout << "Running on " << numThreads << " cores." << std::endl;
	std::vector<std::thread> workers;
	// Divide up the bound so all workers get as close to equal numbers as possible
	int extras = game_count%numThreads;
	for (int i = 0; i < numThreads; ++i) {
		long my_game_count = game_count/numThreads+(i<extras);
		workers.emplace_back(workerThread,
							 std::ref(statsMutex), deck_type,
							 deck_size, my_game_count,
							 bound, std::ref(lengths), std::ref(long_games),
							 std::ref(loop_sizes), std::ref(longest),
							 std::ref(longest_game));
	}
		
	// Join all worker threads.
	for (std::thread &t : workers)
		t.join();

	// Print results
	print_results(lengths, long_games, loop_sizes, longest, longest_game);
}

// Play game with a specified starting deck
void play_one_game(pile deck){
	int deck_size = deck.size();
	array<long,2> stats = play_bound_game(deck,deck_size);
	if (stats[1]){
		std::cout << "Found infinite game! Loop size: " << stats[1] << std::endl;
	}
	else {
		std::cout << "Game length: " << stats[0] << std::endl;
	}
}

// gives 805 moves, as expected
void play_long_game(
	array<int,52> long_cards={0,0,0,1,2,0,0,0,3,0,4,0,0,0,0,4,0,1,0,3,0,0,0,2,3,
	0,0,1,0,0,0,0,0,0,0,0,0,0,0,4,1,2,4,0,0,0,0,3,0,0,0,2}
	){
	pile long_deck(52);
	for (int i : long_cards){
		long_deck.push_back(i);
	}
	long_deck = undeal(long_deck);
	play_one_game(long_deck);
}

// Main function to execute the program
int main() {
	// Bound for the number of moves before switching strategy
	// (to catch infinite loops)
	const long bound = 1000; // 1000 is the default argument
	// Deck configuration: Number of each card type (change this to be input, I guess)
	const array<const int,5> deck_type = {36,4,4,4,4};
	// Game count for when we want to play lots of random games with a large deck
	const long game_count = 1000000000;

	// auto start = std::chrono::high_resolution_clock::now();
	// play_all_games_parallel(deck_type, game_count);
	// auto stop = std::chrono::high_resolution_clock::now();
	// auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(stop-start);
	// std::cout << "Time (in milliseconds): " << duration.count() << std::endl;
	
	array<int,52> very_long_cards={0,0,0,3,0,0,0,2,0,3,2,4,1,0,0,0,0,0,4,4,1,0,0,1,0,0,
	0,0,0,0,0,0,0,0,0,0,2,0,0,0,0,3,2,0,1,0,0,0,0,0,3,4};
	play_long_game(very_long_cards);
    return 0;
}