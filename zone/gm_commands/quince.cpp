#include "../client.h"
#include "../Quince.h"

using namespace std;

void command_quince(Client *c, const Seperator *sep)
{
	if (!c) {
		return;
	} // Crash Suppressant: No client. How did we get here?

	const char *usage_string = "Usage: #quince [client]";

	if ((!sep) || (sep->argnum == 0)) {
		c->Message(Chat::White, usage_string);
		return;
	}

	// Case insensitive commands (List == list == LIST)
	strlwr(sep->arg[1]);

	// "quince add" -- Add quest/node.
	//
	// This is a debug/development command, as quests are normally added via
	// activation triggers.
	//
	// 		usage:   add quest 1
	// 				 add node 13

	if (strcmp(sep -> arg[1], "add") == 0)
	{
		if (strcasecmp(sep -> arg[2], "quest") == 0)
		{
			if (sep -> IsNumber(3))
			{
				// Try to look up the root node for this quest.
				int quest_id = atoi(sep -> arg[3]);

				int root_node = Quince::Instance().get_root_node(quest_id);

				if (root_node != -1)
				{
					Quince::Instance().test_add(c, root_node);
					return;
				}
			}

			c -> Message(Chat::White, "usage:  quince add quest <id>");
			return;
		}

		if (strcasecmp(sep -> arg[2], "node") == 0)
		{
			if (sep -> IsNumber(3))
			{
				// Try to look up the root node for this quest.
				int node_id = atoi(sep -> arg[3]);
				Quince::Instance().test_add(c, node_id);
				return;
			}
		}
	}

	// "quince client"  --  Show client state
	// 		Active quest nodes (and their sequence number)
	// 		Active triggers
	// 		Required triggers
	// 		Names/Titles of active quests
	if (strcmp(sep->arg[1], "client") == 0)
	{
		int cid = c -> CharacterID();
		Quince::Instance().show_client(cid);
		return;
	}

	// list quest info
	// 		list quests -- list all quests in the database
	if (strcmp(sep->arg[1], "list") == 0)
	{
		if (strcasecmp(sep->arg[2], "quests") == 0)
		{
			string query("SELECT quest_id, title FROM quince_quests");

			auto results = database.QueryDatabase(query);

			if (! results.Success())
			{
				c->Message(Chat::White, "query failed");
				return;
			}

			for (auto row : results)
			{
				int id = atoi(row[0]);
				string title = row[1];

				c->Message(Chat::White, "quest %d:  %s", id, title);
			}
			c -> Message(Chat::White, "%d quests available", results.RowCount());
			return;
		}

		c -> Message(Chat::White, "usage:  quince list quests");
		return;
	}

	c->Message(Chat::White, usage_string);
}

