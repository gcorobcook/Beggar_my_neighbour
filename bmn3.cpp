// To do: saving and loading saves.
// (Is the right metric cards played, turns,...?)
// Probably collect and return a few metrics: cards, turns, drinks,...
// play_all_games should return stats on each of these
// Return as a struct to main, or whoever, for further analysis.

// To run: g++ -I"C:\Program Files\boost\boost_1_82_0" bmn3.cpp -o bmn3 to compile
// then ./bmn3 to run.

// Multi-threading brings the standard (6,2,2,2,2) pack from ~15 up to ~58 seconds,
// running on 8 cores.
// On the (36,4,4,4,4) pack, 1,000,000 games, it goes from ~14.5 to ~10.5. Progress!

// Conclusions: I should probably pursue different strategies for the whole-pack and
// random cases. In the former case, I can batch games into, say, 100,000 at a time.
// (I'll need to experiment a bit with batch sizes. Can I do something clever with
// deck_type to jump ahead many permutations at a time, to quickly find the
// first permutation of a batch and then let the thread take it from there?)
// In the latter case, each thread can generate games independently,
// combining results at the end.

// The latter is done in bmn_random_parallel; the former I will leave for now.

#include <iostream>
#include <vector>
#include <algorithm>
#include <random>
#include <tuple>
#include <array>
#include <queue>
#include <boost/circular_buffer.hpp>
#include <map>
#include <unordered_map>
#include <limits>
#include <chrono>
#include <thread> // I want to play games in parallel
#include <mutex>
#include <condition_variable>
#include <atomic>

using std::array;

// Define a new type alias for circular_buffer<short int>
using pile = boost::circular_buffer<short>;

// Print the pile
void print_pile(pile &q){
	array<const char,5> card_values = {'0','J','Q','K','A'};
    for (short i : q){
		std::cout << card_values[i];
	}
	// for (short i=q.size()-1; i>=0; --i){
		// std::cout << card_values[q[i]];
	// }
    std::cout << '\n';
}

void print_game_lengths(std::vector<long> &q){
	std::cout << "Number of games by length: ";
	for (long i : q){
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
const short count_deck(array<const short,5> &deck_type){
	short deck_size = 0;
	for (short i : deck_type){
		deck_size += i;
	}
	return deck_size;
}

// Function to generate the initial sorted deck
pile first_deck(array<const short,5> &deck_type, const short &deck_size) {
    pile deck(deck_size);
    short current = 0;
    for (short i : deck_type) {
        for (short j=0; j<i; j++){
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
	for (short i = deck.size() - 1; i > 0; --i) {
        std::swap(deck[i], deck[rng() % (i + 1)]);
    }
}

// Deal the deck to the players; just two players for now
void deal(pile &deck, array<pile*,2> hands){
	bool turn = false;
	for (short card : deck){
		(*hands[turn]).push_back(card);
		turn = !turn;
	}
}

// Undeal the cards from the two hands, concatenated, to get the starting deck back
// I'm not expecting to use this one much, so speed isn't an issue here.
// It's just for convenience when I want to input a specific starting game.
// (What happens if the deck_size is odd? Check C++ integer division.)
pile undeal(pile &deck){
	const short deck_size = deck.size();
	pile deck2(deck_size);
	for (short i=0; i<deck_size; i++){
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

void play_turn(array<pile*,2> hands, pile &played, bool &turn, const short &deck_size){
	short counter = 0;
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
	for (short card:played){
		(*hands[turn]).push_back(card);
	}
	played.clear();
}

// Function to play a game of Beggar My Neighbour and return the number of moves
long bmn_game(pile &deck, const short &deck_size, const long &bound){
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
pile marked_concat(array<pile*,2> hands, bool &turn, const short &deck_size){
	pile this_hand(deck_size+1);
	for (short i : *hands[turn]){
		this_hand.push_back(i);
	}
	this_hand.push_back(-1);
	for (short i : *hands[!turn]){
		this_hand.push_back(i);
	}
	return this_hand;
}

// To hash the marked_concats to search the history in play_bound_game
struct PileHash {
    std::size_t operator()(const pile& p) const {
        std::size_t h = 0;
        // Combine the hash of each element in the pile.
        for (short i : p) {
            // A simple hash combination: you may want something more robust.
            h = h * 31 + std::hash<short>()(i);
        }
        return h;
    }
};

// Play games whose length is >bound to check for loops.
// In general I should set bound so that most games passed to this have loops.
// I want to return the loop length, hands to reach the loop, starting deck,...
// I expect few long games, so speed is less important here.
array<long,2> play_bound_game(pile &deck, const short &deck_size){
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
		// for (short i=0; i<seen.size(); i++){
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

// A simple thread-safe queue template.
template <typename T>
class ConcurrentQueue {
public:
	void push(const T& value) {
		std::lock_guard<std::mutex> lock(m_mutex);
		m_queue.push(value);
		m_cond.notify_one();
	}

	// Blocks until an element is available or 'done' is true.
	bool pop(T &value, std::atomic<bool>& done) {
		std::unique_lock<std::mutex> lock(m_mutex);
		// Wait until there is an element or the producer is done.
		m_cond.wait(lock, [&] { return !m_queue.empty() || done.load(); });
		if (!m_queue.empty()) {
			value = m_queue.front();
			m_queue.pop();
			return true;
		}
		return false;
	}

	bool empty() {
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_queue.empty();
	}
	
	void wake_threads(){
		m_cond.notify_all();
	}
    
private:
	std::queue<T> m_queue;
	std::mutex m_mutex;
	std::condition_variable m_cond;
};

// Each worker thread should check if there are games to play and play one.
void workerThread(ConcurrentQueue<pile>& deckQueue,
	std::atomic<bool>& done,
	std::mutex& statsMutex,
	const short deck_size,
	const long bound,
	std::vector<long>& lengths,
	std::map<long, long>& long_games,
	std::map<long, long>& loop_sizes,
	long& longest,
	pile& longest_game){
	while (true) {
		pile currentDeck;
		if (!deckQueue.pop(currentDeck, done)) {
			// No work available and producer is done.
			break;
		}
		// Each simulation is independent.
		long turns = bmn_game(currentDeck, deck_size, bound+1);
		if (turns > bound){
			array<long,2> stats = play_bound_game(currentDeck,deck_size);
			// Update long game statistics
			{std::lock_guard<std::mutex> lock(statsMutex);
				if (stats[1]){ // Found a loop!
					// std::cout << "Found a loop!" << std::endl;
					loop_sizes[stats[1]]++;
					longest = std::numeric_limits<long>::max();
					longest_game = currentDeck;
				}
				else { // Found a long finite game!
					// std::cout << "Found a long game!" << std::endl;
					long_games[stats[0]]++;
					if (stats[0]>longest){
						longest = stats[0];
						longest_game = currentDeck;
					}
				}
			}
		}
		// Update statistics
		{std::lock_guard<std::mutex> lock(statsMutex);
			++lengths[turns];
			if (turns > longest){
				longest = turns;
				longest_game = currentDeck;
			}
		}
	}
}

// Function to play some random games or all games of a given deck_type
// Pass with one argument, or with game_count=0, to play all games of type deck_type
void play_all_games_parallel(array<const short,5> &deck_type,
	const long &game_count=0,
	const long &bound=1000){
	// Initialise
	std::mutex statsMutex;
	std::vector<long> lengths(bound+2);
	std::map<long, long> long_games;
	std::map<long, long> loop_sizes;
	long longest=0;
	pile longest_game(52);
	
	const short deck_size = count_deck(deck_type);
	
	// Get starting deck
	pile deck=first_deck(deck_type, deck_size);
	if (game_count){
		random_deck(deck);
	}
	
	// The work queue.
    ConcurrentQueue<pile> deckQueue;
	
	// Producer flag.
    std::atomic<bool> done(false);
	bool flag=true;
	long i=0; // I guess this will be implemented through done
	
	// Launch worker threads.
	const unsigned int numThreads = std::thread::hardware_concurrency();
	std::cout << "Running on " << numThreads << " cores." << std::endl;
	std::vector<std::thread> workers;
	for (unsigned int i = 0; i < numThreads; ++i) {
		workers.emplace_back(workerThread, std::ref(deckQueue), std::ref(done),
							 std::ref(statsMutex), deck_size, bound,
							 std::ref(lengths), std::ref(long_games),
							 std::ref(loop_sizes), std::ref(longest),
							 std::ref(longest_game));
	}

    // Producer: generate decks and push them into the queue,
	// until deck is empty or we reach the bound.
    while (flag){
        deckQueue.push(deck);
		if (game_count){
			random_deck(deck); // ready for next game
			++i;
			flag = (i<game_count); // change flag if done
		}
		else {
			next_deck(deck); // ready for next game
			flag = !deck.empty(); // change flag if done
		}
    }
	done.store(true);
	
	// Wake all threads (if they are waiting) (if needed).
	deckQueue.wake_threads();
	
	// Join all worker threads.
	for (auto &t : workers)
		t.join();

	// Print results
	print_results(lengths, long_games, loop_sizes, longest, longest_game);
}

// Play game with a specified starting deck
void play_one_game(pile deck){
	short deck_size = deck.size();
	array<long,2> stats = play_bound_game(deck,deck_size);
	if (stats[1]){
		std::cout << "Found infinite game! Loop size: " << stats[1] << std::endl;
	}
	else {
		std::cout << "Game length: " << stats[0] << std::endl;
	}
}

// gives 805 moves, as expected
void play_long_game(){
	array<short,52> long_cards={0,0,0,1,2,0,0,0,3,0,4,0,0,0,0,4,0,1,0,3,0,0,0,2,3,
	0,0,1,0,0,0,0,0,0,0,0,0,0,0,4,1,2,4,0,0,0,0,3,0,0,0,2};
	pile long_deck(52);
	for (short i : long_cards){
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
	array<const short,5> deck_type = {6,2,2,2,2};
	// Game count for when we want to play lots of random games with a large deck
	const long game_count = 1000000;

	auto start = std::chrono::high_resolution_clock::now();
	// play_all_games_parallel(deck_type, game_count);
	play_all_games_parallel(deck_type);
	auto stop = std::chrono::high_resolution_clock::now();
	auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(stop-start);
	std::cout << "Time (in milliseconds): " << duration.count() << std::endl;
	
    return 0;
}