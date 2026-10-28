#pragma once

#include <functional>
#include <string>
#include <vector>

#include "GameUI.hpp"

// The in game command console: opened with the tilde key, type a command (e.g. "grant torch 16", "tod 12:20",
// "tp 100,50,200" or "help"). Commands have one or more names ("teleport", "tp"), are matched fuzzily, & show their
// arguments as a hint (optional ones in [brackets], with their default: "[count=1]").
//
// Other systems add commands with GameConsole::Get().Register(...) (once, e.g. from their Initialize):
//
//   GameConsole::Get().Register({ "heal", "hp" }, "[amount=100]", "Restores health",
//       [](const std::vector<std::string>& args) -> std::string { player->Heal(ParseFloat(args[0])); return "Healed"; });
//
// The usage lists the arguments: <name> is required, [name=default] is optional. The command is run with exactly
// one string per argument (defaults filled in), after the line is split at spaces & commas (so "tp 1,2,3" works).
// Whatever it returns is printed in the console (return "" for nothing; start with "!" for an error).
namespace flex
{
	class GameConsole
	{
	public:
		using RunFunction = std::function<std::string(const std::vector<std::string>& args)>;
		// Possible values for argument argIndex (shown as suggestions, Tab completes them)
		using SuggestFunction = std::function<std::vector<std::string>(u32 argIndex)>;
		// Gives count of the named item to the player. Returns the message to show.
		using GrantFunction = std::function<std::string(i32 count)>;

		static GameConsole& Get();

		void Register(const std::vector<std::string>& names, const std::string& usage, const std::string& description,
			RunFunction run, SuggestFunction suggest = nullptr);
		// Makes "grant <name> [count]" work for something that isn't a prefab item (e.g. "torch")
		void RegisterGrantable(const std::string& name, GrantFunction give);
		std::vector<std::string> GetGrantableNames() const;
		// Gives a grantable by (fuzzy) name. Returns false if there's no match.
		bool Grant(const std::string& name, i32 count, std::string& outMessage) const;

		// Runs a line as if it was typed, returns the output
		std::string Execute(const std::string& line);
		void Log(const std::string& text, bool bError = false);

		bool IsOpen() const { return m_bOpen; }
		void SetInput(const std::string& input) { m_Input = input; m_SuggestionBase.clear(); } // For tests
		void SetOpen(bool bOpen);
		// Handles typing & draws the console (and recent output while closed)
		void Draw(GameUI& ui);

		// How well query matches text: 0 for none, higher is better (exact, prefix, substring, then subsequence)
		static i32 FuzzyScore(const std::string& query, const std::string& text);
		// The best match among candidates, or -1 if none (or two are equally good, when bRequireUnique)
		static i32 BestMatch(const std::string& query, const std::vector<std::string>& candidates, bool bRequireUnique);

	private:
		struct Arg
		{
			std::string name;
			bool bOptional = false;
			std::string defaultValue;
		};
		struct Command
		{
			std::vector<std::string> names;
			std::string usage;
			std::string description;
			std::vector<Arg> args;
			RunFunction run;
			SuggestFunction suggest;
		};
		struct Line
		{
			std::string text;
			bool bError = false;
			real time = 0.0f;
		};

		GameConsole();
		void RegisterBuiltins();
		static std::vector<std::string> Tokenize(const std::string& line);
		const Command* FindCommand(const std::string& word, bool bFuzzy) const;
		// (text, detail) pairs: command names, or values for the argument being typed
		std::vector<std::pair<std::string, std::string>> Suggestions(const std::string& input) const;
		static std::string ApplySuggestion(const std::string& input, const std::string& suggestion);

		std::vector<Command> m_Commands;
		std::vector<std::pair<std::string, GrantFunction>> m_Grantables;
		std::vector<Line> m_Log;
		std::vector<std::string> m_History;
		i32 m_HistoryIndex = -1;
		std::string m_Input;
		i32 m_SuggestionIndex = 0;
		std::string m_SuggestionBase; // The input the suggestions were made for
		bool m_bOpen = false;
		real m_Time = 0.0f;
		real m_OpenTime = 0.0f;
	};
} // namespace flex
