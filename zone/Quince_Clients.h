#ifndef QUINCE_CLIENT_H
#define QUINCE_CLIENT_H

#include <map>
#include <memory>	// for unique_ptr
#include <set>
#include <vector>
#include "entity.h"
#include "Quince_Triggers.h"
#include "event_codes.h"


// forward decls
class Client;
class Quest_Node;
class Client_State;
class Timer_Binding;
class Entity_Registration;
class Variable_Binding;

struct NPC_Waypoint_State
{
	int eid;			// npc entity id
	int position;		// last waypoint departed
	NPC *npc;
	Client *client;
	int char_id;
};

typedef std::map<int, int> 				Node_ID_Map;
typedef std::map<int, int> 				Trigger_ID_Map;
typedef std::multimap<int, int> 		Trigger_ID_Set;
typedef std::map<int, std::string> 		String_Map;
typedef std::vector<std::string>    	String_List;
typedef std::map<int, int> 				Integer_Map;
typedef std::vector<NPC_Waypoint_State>	NPC_WPs;
typedef std::list<int>	 				ID_List;
typedef std::map<int, bool>				Bool_Map;

class Client;

class Client_State
{
	private:
		Client *client;						// not persisted

		int character_id;
		Node_ID_Map node_seq;				// node -> seq
		Trigger_ID_Map trigger_state;		// trigger -> state
		Trigger_ID_Set active_triggers;		// node -> trigger set
		Trigger_ID_Set required_triggers;	// req for node completion
		Bool_Map satisfied;
		Integer_Map roll_back;				// per-node roll-back points

		// data for quest journal
		String_List activity_list;
		String_Map quest_desc;				// quest-id -> description
		String_Map quest_title;				// quest-id -> title
		Integer_Map quest_seq;				// quest -> client index
		Integer_Map journal_slots;			// trigger -> slot
		Integer_Map pending_complete;		// quest -> triggers awaiting completion

		ID_List active_quests;				// quests held by client
		ID_List polled_triggers;			// active, polled triggers
		ID_List fired_triggers;				// mainly for repeatable triggers

		NPC_WPs walking_npcs;

		bool on_node(int node_id);
		bool has_fired(int trigger_id);
		void remove_active_trigger(int trigger_id);
		void remove_active_triggers(int node_id);

		int seq_for_node(int node_id) const;

		void save_trigger(int node_id, int trigger_id);
		void save_state();
		void save_active_triggers();
		void load_triggers_at_seq(int node_id, int seq);

		int zone_from_db(int trigger_id);

		// quest journal stuff
		void send_task_description(int quest_id, bool show_journal=true);
		void cancel_task(int quest_id);
		void schedule_activity_complete(int quest_id, int trigger_id);
		bool is_journaled(int trigger_id);
	public:
		Client_State();
		~Client_State();

		void display() const;
		void unload();

		inline Client *get_client() 	{return client;}

		bool has_quest(int quest_id);
		bool is_active_trigger(int trigger_id);

		void process_global_event(Event *event, int quest_id);

		void send_task(int quest_id);
		void send_task_state(int quest_id);
		void send_visible_activity(int slot, int trigger_id);
		void send_activity_complete(int quest_id, int trigger_id, bool last);

		// load state from the database (larc_active_triggers)
		void load_state(Client *c, int zone); 
		int get_trigger_state(int trigger_id);
		void set_trigger_state(int trigger_id, int state);
		bool is_satisfied(int trigger_id);
		void set_satisfied(int trigger_id);

		bool start_quest(int root_node);
		void cancel_quest(int seq_id);		// client index (quest_seq)
		void fail_quest(int quest_id);
		void complete_trigger(int trigger_id);

		void advance_seq(int node_id, int trigger_id);

		bool is_visible(int quest, int trigger);
		void process();

		void set_rollback(int node_id, int seq);
		void clear_rollback(int node_id);

		bool get_waypoint_state(int npc_eid, NPC_Waypoint_State& state);
		void remove_waypoint_state(int npc_eid);
		void init_waypoints(int npc_eid);
};
#endif
