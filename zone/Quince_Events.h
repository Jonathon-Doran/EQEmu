/*	EQEMu: Everquest Server Emulator
	Copyright (C) 2001-2002 EQEMu Development Team (http://eqemu.org)

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; version 2 of the License.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY except by those people which sell it, which
	are required to give you total support for your newly bought product;
	without even the implied warranty of MERCHANTABILITY or FITNESS FOR
	A PARTICULAR PURPOSE. See the GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#ifndef QUINCE_EVENTS_H
#define QUINCE_EVENTS_H

#include <string>
#include "mob.h"
#include "client.h"
#include "event_codes.h"
// #include "../common/any.h"

#include "entity.h"						// for skills
class Event;
#include "Quince_Triggers.h"			// for EntityDescriptor

extern std::string event_string(QuestEventID id);

// Event has enough information to generate an EventNPC/EventPlayer/EventItem/EventSpell call
// Additional data is sometimes needed for preprocessing
class Event
{
	private:
		int id;									// may be 0 for old quest code
		QuestEventID type;						// the enum which identifies this event type

	protected:
		// Some events need additional script variables set before the script is ran
		// This is where we ask the Event objects to dump their variables
		Client *client;
		virtual void ExportAdditionalVariables()	{};


	public:
		Event (QuestEventID id);

		// two different naming conventions for events.  Let them identify themselves
		// I don't think the extra memory usage is worse than asking the CPU to change case
		// (besides, the previous implementation used two sets of constants)
		virtual std::string Event_Name_UC() = 0;
		virtual std::string Event_Name_LC() = 0;

		// the event type is not really an id
		inline QuestEventID GetType()	{return type;}

		virtual int Post() = 0;					// post the event to one or more scripts
		uint32 getCharacterID();
		inline void setClient(Client *c)
		{
			this -> client = c;
		}
		inline Client* getClient()
		{
			return client;
		}
};

// See Client::ChannelMessageReceived -- when a message is received on a the say channel
// Either an EVENT_PLAYER, or EVENT_NPC
class Event_Say : public Event
{
	public:
		Event_Say();
		int Post();
		void ExportAdditionalVariables();

		std::string Event_Name_UC()   	{return "EVENT_SAY";}
		std::string Event_Name_LC()		{return "event_say";}

		inline void setTarget(NPC *target)
		{
			this -> target = target;
		}
		inline NPC* getTarget()
		{
			return target;
		}

		uint32 getNPCID();
		inline void setText(std::string text)
		{
			this -> text = text;
		}
		inline std::string getText()
		{
			return text;
		}

		inline void setLanguage(uint8 language_id)
		{
			this -> language_id = language_id;
		}
		inline uint8 getLanguage()
		{
			return language_id;
		}

		uint32 getCharacterID();
	private:
		NPC *target;

		std::string text;
		uint8 language_id;
};

// NPC in the event is the client's trading partner
class Event_Trade : public Event
{
	public:
		Event_Trade();
		int Post();
		void ExportAdditionalVariables();

		std::string Event_Name_UC()   	{return "EVENT_TRADE";}
		std::string Event_Name_LC()		{return "event_trade";}

		inline void setNPC(NPC *npc)
		{
			this -> npc = npc;
		}

		inline void setItems(std::vector<std::any> items_traded)
		{
			this -> items_traded = items_traded;
		}

	private:
		NPC *npc;
		std::vector<std::any> items_traded;
};

class Event_Spawn : public Event
{
	public:
		Event_Spawn();
		int Post();
		void ExportAdditionalVariables();

		inline void setNewNPC(NPC *new_npc)
		{
			this -> new_npc = new_npc;
		}
		uint32 getNPCID();

		std::string Event_Name_UC()   	{return "EVENT_SPAWN";}
		std::string Event_Name_LC()		{return "event_spawn";}
	private:
		NPC *new_npc;
};

class Event_Attack : public Event
{
	public:
		Event_Attack();
		int Post();
		void ExportAdditionalVariables();

		inline void setTarget(NPC *target_npc)
		{
			this -> target_npc = target_npc;
		}

		inline void setAttacker(Mob *attacking_mob)
		{
			this -> attacking_mob = attacking_mob;
		}


	private:
		NPC *target_npc;
		Mob *attacking_mob;
};

class Event_Combat : public Event
{
	public:
		enum Threshold_Type {Enter_Combat, Leave_Combat};

		Event_Combat();
		int Post();
		void ExportAdditionalVariables();

		std::string Event_Name_UC()   	{return "EVENT_COMBAT";}
		std::string Event_Name_LC()		{return "event_combat";}

		inline void setNPC(NPC *combat_npc)
		{
			this -> combat_npc = combat_npc;
		}

		inline void setAttacker(Mob *attacker)
		{
			this -> attacker = attacker;
		}

		inline void setTransition(Threshold_Type transition)
		{
			this -> transition = transition;
		}

	private:
		Threshold_Type  transition;
		NPC *combat_npc;
		Mob *attacker;							// nullptr when leaving combat
};

class Event_Aggro : public Event
{
	public:
		Event_Aggro();
		int Post();
		void ExportAdditionalVariables();
	
		std::string Event_Name_UC()   	{return "EVENT_AGGRO";}
		std::string Event_Name_LC()		{return "event_aggro";}

		inline void setAggroingNPC(NPC *aggroing_npc)
		{
			this -> aggroing_npc = aggroing_npc;
		}

	private:
		NPC *aggroing_npc;
};

class Event_Slay : public Event
{
	public:
		Event_Slay();
		int Post();
		void ExportAdditionalVariables();

	private:
		NPC *killer_npc;
};

class Event_Waypoint_Arrive : public Event
{
	public:
		Event_Waypoint_Arrive();
		int Post();
		void ExportAdditionalVariables();

		std::string Event_Name_UC()   	{return "EVENT_WAYPOINT_ARRIVE";}
		std::string Event_Name_LC()		{return "event_waypoint_arrive";}

		inline void setNPC(NPC *npc)
		{
			this -> npc = npc;
		}
		inline uint32 getNPCID()
		{
			if (npc)
			{
				return npc -> GetID();
			}
			else
			{
				return 0;
			}
		}
		inline void setWaypointID(uint32 waypoint_id)
		{
			this -> waypoint_id = waypoint_id;
		}

		inline void setEID(int npc_eid)
		{
			this -> npc_eid = npc_eid;
		}
		inline int getEID()		{return npc_eid;}

	private:
		NPC *npc;												// wandering NPC
		uint32 waypoint_id;
		int npc_eid;

};

class Event_Waypoint_Depart : public Event
{
	public:
		Event_Waypoint_Depart();
		int Post();
		void ExportAdditionalVariables();

		std::string Event_Name_UC()   	{return "EVENT_WAYPOINT_DEPART";}
		std::string Event_Name_LC()		{return "event_waypoint_depart";}

		inline void setNPC(NPC *npc)
		{
			this -> npc = npc;
		}

		inline void setWaypointID(uint32 waypoint_id)
		{
			this -> waypoint_id = waypoint_id;
		}

	private:
		NPC *npc;												// wandering NPC
		uint32 waypoint_id;

};

class Event_Timer : public Event
{
	public:
		Event_Timer();
		int Post();
		void ExportAdditionalVariables();

		std::string Event_Name_UC()   	{return "EVENT_TIMER";}
		std::string Event_Name_LC()		{return "event_timer";}

		inline void setNPC(NPC *npc)
		{
			this -> npc = npc;
		}

		inline void setItem(EQ::ItemInstance *item)
		{
			this -> item = item;
		}

		inline void setTimer(std::string timer_name)
		{
			this -> timer_name = timer_name;
		}

	private:
		NPC *npc;
		EQ::ItemInstance *item;
		std::string timer_name;

};

class Event_Signal : public Event
{
	public:
		Event_Signal();
		int Post();
		void ExportAdditionalVariables();

		inline void setSignalNPC(NPC *npc)
		{
			this -> npc = npc;
		}
		inline void setSignalID(int signal_id)
		{
			this -> signal_id = signal_id;
		}
		inline int getSignalID()		{return signal_id;}

		inline void setQuestID(int quest_id)
		{
			this -> quest_id = quest_id;
		}
		inline int getQuestID()			{return quest_id;}

		uint32 getCharacterID();

		std::string Event_Name_UC()   	{return "EVENT_SIGNAL";}
		std::string Event_Name_LC()		{return "event_signal";}

	private:
		NPC *npc;
		int signal_id;
		int quest_id;
};

// I'm not sure why we have this in addition to NPC_Death
// This was added for an npc killing another npc
// an NPC's HP goes above or below some preset limits
class Event_HP : public Event
{
	public:
		enum Threshold_Type {Above_Threshold, Below_Threshold};

		Event_HP();
		int Post();
		void ExportAdditionalVariables();

		std::string Event_Name_UC()   	{return "EVENT_HP";}
		std::string Event_Name_LC()		{return "event_hp";}

		inline void setNPC(NPC *npc)
		{
			this -> npc = npc;
		}

		inline void setTriggerHP(int trigger_hp)
		{
			this -> trigger_hp = trigger_hp;
		}

		inline void setThresholdType(Threshold_Type threshold_type)
		{
			this -> threshold_type = threshold_type;
		}

	private:
		NPC *npc;
		int trigger_hp;											// the value which was passed (see Mob::SetNextHPEvent)
		Threshold_Type threshold_type;

};

class Event_Enter : public Event
{
	public:
		Event_Enter();
		int Post();
		void ExportAdditionalVariables();

		inline void setNPC(NPC *npc)
		{
			this -> npc = npc;
		}

	private:
		NPC *npc;
};

class Event_Exit : public Event
{
	public:
		Event_Exit();
		int Post();
		void ExportAdditionalVariables();

		inline void setNPC(NPC *npc)
		{
			this -> npc = npc;
		}


	private:
		NPC *npc;
};

class Event_Enter_Zone : public Event
{
	public:
		Event_Enter_Zone();
		int Post();
		void ExportAdditionalVariables();
};

// I'm not sure why we have this in addition to NPC_Death
// This was added for an npc killing another npc
class Event_NPC_Slay : public Event
{
	public:
		Event_NPC_Slay();
		int Post();
		void ExportAdditionalVariables();

		std::string Event_Name_UC()   	{return "EVENT_NPC_SLAY";}
		std::string Event_Name_LC()		{return "event_npc_slay";}

		inline void setDeadNPC(NPC *dead_npc)
		{
			this -> dead_npc = dead_npc;
		}

		inline void setKillerNPC(NPC *killer_npc)
		{
			this -> killer_npc = killer_npc;
		}


	private:
		NPC *dead_npc;
		NPC *killer_npc;

};

// see Handle_OP_ClickDoor
class Event_Click_Door : public Event
{
	public:
		Event_Click_Door();
		int Post();
		void ExportAdditionalVariables();

		std::string Event_Name_UC()   	{return "EVENT_DOOR_CLICK";}
		std::string Event_Name_LC()		{return "event_door_click";}

		inline void setDoor(uint8 door_id)
		{
			door = entity_list.FindDoor(door_id);
			if (!door)
			{
			}
		}

	private:
		uint8 door_id;					        // from the ClickDoor_Struct, passed in from client
		Doors *door;					        // the clicked door

};

// See Corpse::LootItem
class Event_Loot : public Event
{
	public:
		// this will generate BOTH EventPlayer and EventItem for the client and the item looted
		// see Corpse::LootItem()

		Event_Loot();
		int Post();
		void ExportAdditionalVariables();

		std::string Event_Name_UC()   	{return "EVENT_LOOT";}
		std::string Event_Name_LC()		{return "event_loot";}

		inline void setItem(EQ::ItemInstance *item_inst)
		{
			this -> item_inst = item_inst;
		}

		inline void setCorpse(Corpse *corpse)
		{
			this -> corpse = corpse;
		}

	private:
		EQ::ItemInstance *item_inst;					// the item just looted
		Corpse *corpse;							// corpse being looted
};

// see Client::Handle_OP_ZoneChange
class Event_Zone : public Event
{
	public:
		Event_Zone();
		int Post();
		void ExportAdditionalVariables();

		std::string Event_Name_UC()   	{return "EVENT_ZONE";}
		std::string Event_Name_LC()		{return "event_zone";}

		inline void setDestinationZone(uint32 target_zone_id)
		{
			this -> target_zone_id = target_zone_id;
		}


	private:
		uint32 target_zone_id;
};

class Event_Level_Up : public Event
{
	public:
		Event_Level_Up();
		int Post();
		void ExportAdditionalVariables();

		std::string Event_Name_UC()   	{return "EVENT_LEVEL_UP";}
		std::string Event_Name_LC()		{return "event_level_up";}
};

class Event_Killed_Merit : public Event
{
	public:
		Event_Killed_Merit();
		int Post();
		void ExportAdditionalVariables();

		std::string Event_Name_UC()   	{return "EVENT_KILLED_MERIT";}
		std::string Event_Name_LC()		{return "event_killed_merit";}

	private:
		NPC *npc;

};

// See Mob::SpellOnTarget()
class Event_Cast_On : public Event
{
	public:
		Event_Cast_On();
		int Post();
		void ExportAdditionalVariables();

		std::string Event_Name_UC()   	{return "EVENT_CAST_ON";}
		std::string Event_Name_LC()		{return "event_cast_on";}

		inline void setTarget(NPC *target)
		{
			this -> target = target;
		}

		inline void setCaster(Mob *caster)
		{
			this -> caster = caster;
		}

		inline void setSpellID(uint16 spell_id)
		{
			this -> spell_id = spell_id;
		}

	private:
		NPC *target;
		Mob *caster;				// also initiator, but I want to make this role clear when used
		uint16 spell_id;
};

class Event_Task_Accepted : public Event
{
	public:
		Event_Task_Accepted();
		int Post();
		void ExportAdditionalVariables();

		std::string Event_Name_UC()   	{return "EVENT_TASK_ACCEPTED";}
		std::string Event_Name_LC()		{return "event_task_accepted";}

		inline void setTaskID(int task_id)
		{
			this -> task_id = task_id;
		}

	private:
		NPC *task_giver;
		int task_id;
};

class Event_Task_Stage_Complete : public Event
{
	public:
		Event_Task_Stage_Complete();
		int Post();
		void ExportAdditionalVariables();

		std::string Event_Name_UC()   	{return "EVENT_TASK_STAGE_COMPLETE";}
		std::string Event_Name_LC()		{return "event_task_stage_complete";}

		inline void setTaskID(int task_id)
		{
			this -> task_id = task_id;
		}

		inline void setActivityID(int activity_id)
		{
			this -> activity_id = activity_id;
		}


	private:
		int task_id;
		int activity_id;
};

class Event_Task_Update : public Event
{
	public:
		Event_Task_Update();
		int Post();
		void ExportAdditionalVariables();

		std::string Event_Name_UC()   	{return "EVENT_TASK_UPDATE";}
		std::string Event_Name_LC()		{return "event_task_update";}

		inline void setTaskID(int task_id)
		{
			this -> task_id = task_id;
		}

		inline void setActivityID(int activity_id)
		{
			this -> activity_id = activity_id;
		}

		inline void setDoneCount(int done_count)
		{
			this -> done_count = done_count;
		}


	private:
		int done_count;
		int activity_id;
		int task_id;
};

// See ClientTaskState::IncrementDoneCount
// It is possible to generate both Task_Update and Task_Complete at the same time
class Event_Task_Complete : public Event
{
	public:
		Event_Task_Complete();
		int Post();
		void ExportAdditionalVariables();

		std::string Event_Name_UC()   	{return "EVENT_TASK_COMPLETE";}
		std::string Event_Name_LC()		{return "event_task_complete";}

		inline void setTaskID(int task_id)
		{
			this -> task_id = task_id;
		}

		inline void setActivityID(int activity_id)
		{
			this -> activity_id = activity_id;
		}

		inline void setDoneCount(int done_count)
		{
			this -> done_count = done_count;
		}


	private:
		int task_id;
		int activity_id;
		int done_count;
};

class Event_Task_Fail : public Event
{
	public:
		Event_Task_Fail();
		int Post();
		void ExportAdditionalVariables();

		std::string Event_Name_UC()   	{return "EVENT_TASK_FAIL";}
		std::string Event_Name_LC()		{return "event_task_fail";}

		inline void setTaskID(int task_id)
		{
			this -> task_id = task_id;
		}


	private:
		int task_id;
};

// sent when a mob engages the player and says something (on the say channel)
// See Client::ChannelMessageReceived
class Event_Aggro_Say : public Event
{
	public:
		Event_Aggro_Say();
		int Post();
		void ExportAdditionalVariables();

		std::string Event_Name_UC()   	{return "EVENT_AGGRO_SAY";}
		std::string Event_Name_LC()		{return "event_aggro_say";}

		inline void setSpeaker(NPC *speaker)
		{
			this -> speaker = speaker;
		}

		inline void setMessage(std::string message)
		{
			this -> message = message;
		}

		inline void setLanguage(uint8 language_id)
		{
			this -> language_id = language_id;
		}


	private:
		NPC *speaker;
		std::string message;
		uint8 language_id;
};

// See Object::HandleClick
// Note that the item-id was previously passed as a parameter, but we already have the ItemInst object
class Event_Player_Pickup : public Event
{
	public:
		Event_Player_Pickup();
		int Post();
		void ExportAdditionalVariables();

		std::string Event_Name_UC()   	{return "EVENT_PLAYER_PICKUP";}
		std::string Event_Name_LC()		{return "event_player_pickup";}

		inline void setItem(EQ::ItemInstance *item_inst)
		{
			this -> item_inst = item_inst;
		}

	private:
		EQ::ItemInstance *item_inst;
};

// can be generated for the player, as well as a targetted Mob
class Event_Popup_Response : public Event
{
	public:
		Event_Popup_Response();
		int Post();
		void ExportAdditionalVariables();

		std::string Event_Name_UC()   	{return "EVENT_POPUP_RESPONSE";}
		std::string Event_Name_LC()		{return "event_popup_response";}


		inline void setTarget(NPC *target)
		{
			this -> target = target;
		}

		inline void setPopupID(int popup_id)
		{
			this -> popup_id = popup_id;
		}

	private:
		NPC *target;
		int popup_id;
};

// Quest issued:  mobs set proximites around themselves when they spawn.  They can use this to
// respond when players say things nearby.
//
// See EntityList::ProcessProximitySay
class Event_Proximity_Say : public Event
{
	public:
		Event_Proximity_Say();
		int Post();
		void ExportAdditionalVariables();

		std::string Event_Name_UC()   	{return "EVENT_PROXIMITY_SAY";}
		std::string Event_Name_LC()		{return "event_proximity_say";}

		inline void setNPC(NPC *npc)
		{
			this -> npc = npc;
		}

		inline void setMessage(std::string message)
		{
			this -> message = message;
		}

		inline void setLanguage(uint8 language_id)
		{
			this -> language_id = language_id;
		}

	private:
		NPC *npc;
		std::string message;
		uint8 language_id;
};

// generated when casting is complete
class Event_Cast : public Event
{
	public:
		Event_Cast();
		int Post();
		void ExportAdditionalVariables();

		inline void setCaster(Mob *caster)
		{
			this -> caster = caster;
		}

		inline void setSpellID(uint16 spell_id)
		{
			this -> spell_id = spell_id;
		}

		std::string Event_Name_UC()   	{return "EVENT_CAST";}
		std::string Event_Name_LC()		{return "event_cast";}

	private:
		Mob *caster;							// will either be Client or NPC, appropriate parser call used
		uint16 spell_id;
};

class Event_Cast_Begin : public Event
{
	public:
		Event_Cast_Begin();
		int Post();
		void ExportAdditionalVariables();

		std::string Event_Name_UC()   	{return "EVENT_CAST_BEGIN";}
		std::string Event_Name_LC()		{return "event_cast_begin";}

		inline void setCaster(Mob *caster)
		{
			this -> caster = caster;
		}

		inline void setSpellID(uint16 spell_id)
		{
			this -> spell_id = spell_id;
		}

	private:
		Mob *caster;							// will either be Client or NPC, appropriate parser call used
		uint16 spell_id;
};

// See Client::CalcItemScale
// Called for each scaled item when client zones in, or the charm update timer expires
// Will also be called if a scaled augment is on the item.
// Scaling can be set via script, or if the CharmFileID is set.
// Items such as 'Glowing Green Silver Earring of Dragoste' scale, but are not charms.

class Event_Scale_Calc : public Event
{
	public:
		Event_Scale_Calc();
		int Post();
		void ExportAdditionalVariables();

		inline void setItem(EQ::ItemInstance *item_inst)
		{
			this -> item_inst = item_inst;
		}

		inline void setAugment(EQ::ItemInstance *augment)
		{
			this -> augment = augment;
		}

		std::string Event_Name_UC()   	{return "EVENT_SCALE_CALC";}
		std::string Event_Name_LC()		{return "event_scale_calc";}

	private:
		EQ::ItemInstance *item_inst;
		EQ::ItemInstance *augment;
};

//
// See Client::DoItemEnterZone
// Called when an item enters a zone
// Called for each augment when it enters a zone
//

class Event_Item_Enter_Zone : public Event
{
	public:
		Event_Item_Enter_Zone();
		int Post();
		void ExportAdditionalVariables();

		inline void setItem(EQ::ItemInstance *item_inst)
		{
			this -> item_inst = item_inst;
		}

		std::string Event_Name_UC()   	{return "EVENT_ITEM_ENTER_ZONE";}
		std::string Event_Name_LC()		{return "event_item_enter_zone";}

	private:
		EQ::ItemInstance *item_inst;
};

class Event_Target_Change : public Event
{
	public:
		Event_Target_Change();
		int Post();
		void ExportAdditionalVariables();

		inline void setMob(Mob *mob)
		{
			this -> mob = mob;
		}

		inline void setTarget(Mob *target)
		{
			this -> target = target;
		}
		std::string Event_Name_UC()   	{return "EVENT_TARGET_CHANGE";}
		std::string Event_Name_LC()		{return "event_target_change";}

	private:
		Mob *mob;
		Mob *target;
};

//
// See HateList::Add, HateList::RemoveEnt, HateList::Wipe
//
// Each mob has its own hate list.  This event occurs each time
// a mob enters/leaves the hate list.
//

class Event_Hate_List : public Event
{
	public:
		enum HateList_Change {HateList_Join, HateList_Leave};

		Event_Hate_List();
		int Post();
		void ExportAdditionalVariables();

		inline void setListOwner(NPC *list_owner)
		{
			this -> list_owner = list_owner;
		}

		inline void setMember(Mob *member)
		{
			this -> member = member;
		}

		inline void setStateChange(HateList_Change state_change)
		{
			this -> state_change = state_change;
		}

		std::string Event_Name_UC()   	{return "EVENT_HATE_LIST";}
		std::string Event_Name_LC()		{return "event_hate_list";}

	private:
		NPC *list_owner;										// NPC owning the hatelist
		Mob *member;											// mob joining/leaving the list
		HateList_Change state_change;							// joining or leaving the list
};

// See Mob::SpellEffect.  Called when a spell effect lands on the client
class Event_Spell_Effect_Client : public Event
{
	public:
		Event_Spell_Effect_Client();
		int Post();
		void ExportAdditionalVariables();

		inline void setCaster(Mob *caster)
		{
			this -> caster = caster;
		}

		inline void setTarget(Client *target)
		{
			this -> target = target;
		}

		inline void setSpellID(int spell_id)
		{
			this -> spell_id = spell_id;
		}

		inline void setBuffSlot(int buff_slot)
		{
			this -> buff_slot = buff_slot;
		}

		std::string Event_Name_UC()   	{return "EVENT_SPELL_EFFECT_CLIENT";}
		std::string Event_Name_LC()		{return "event_spell_effect_client";}

	private:
		Client *target;
		Mob *caster;
		int spell_id;
		int buff_slot;
};

// See Mob::SpellEffect.  Called when a spell effect lands on the client
class Event_Spell_Effect_NPC : public Event
{
	public:
		Event_Spell_Effect_NPC();
		int Post();
		void ExportAdditionalVariables();

		inline void setCaster(Mob *caster)
		{
			this -> caster = caster;
		}

		inline void setTarget(NPC *target)
		{
			this -> target = target;
		}

		inline void setSpellID(int spell_id)
		{
			this -> spell_id = spell_id;
		}

		inline void setBuffSlot(int buff_slot)
		{
			this -> buff_slot = buff_slot;
		}

		std::string Event_Name_UC()   	{return "EVENT_SPELL_EFFECT_NPC";}
		std::string Event_Name_LC()		{return "event_spell_effect_npc";}

	private:
		NPC *target;
		Mob *caster;
		int spell_id;
		int buff_slot;
};

// See Mob::DoBuffTic -- called every time a buff tic lands on the client
// Note that we do not always have a caster, in which the id is reported as 0

class Event_Spell_Buff_Tic_Client : public Event
{
	public:
		Event_Spell_Buff_Tic_Client();
		int Post();
		void ExportAdditionalVariables();

		inline void setCaster(Mob *caster)
		{
			this -> caster = caster;
		}

		inline void setTarget(Client *target)
		{
			this -> target = target;
		}

		inline void setSpellID(int spell_id)
		{
			this -> spell_id = spell_id;
		}

		inline void setBuffSlot(int buff_slot)
		{
			this -> buff_slot = buff_slot;
		}

		inline void setTicsRemaining(int tics_remaining)
		{
			this -> tics_remaining = tics_remaining;
		}

		inline void setCasterLevel(int caster_level)
		{
			this -> caster_level = caster_level;
		}

		std::string Event_Name_UC()   	{return "EVENT_BUFF_TIC_CLIENT";}
		std::string Event_Name_LC()		{return "event_buff_tic_client";}

	private:
		Client *target;
		Mob *caster;
		int spell_id;
		int buff_slot;
		int tics_remaining;
		int caster_level;
};

class Event_Spell_Buff_Tic_NPC : public Event
{
	public:
		Event_Spell_Buff_Tic_NPC();
		int Post();
		void ExportAdditionalVariables();

		inline void setCaster(Mob *caster)
		{
			this -> caster = caster;
		}

		inline void setTarget(NPC *target)
		{
			this -> target = target;
		}

		inline void setSpellID(int spell_id)
		{
			this -> spell_id = spell_id;
		}

		inline void setBuffSlot(int buff_slot)
		{
			this -> buff_slot = buff_slot;
		}

		inline void setTicsRemaining(int tics_remaining)
		{
			this -> tics_remaining = tics_remaining;
		}

		inline void setCasterLevel(int caster_level)
		{
			this -> caster_level = caster_level;
		}

		std::string Event_Name_UC()   	{return "EVENT_BUFF_TIC_NPC";}
		std::string Event_Name_LC()		{return "event_buff_tic_npc";}

	private:
		NPC *target;
		Mob *caster;
		int spell_id;
		int buff_slot;
		int tics_remaining;
		int caster_level;
};

class Event_Spell_Fade : public Event
{
	public:
		Event_Spell_Fade();
		int Post();
		void ExportAdditionalVariables();

		inline void setMob(Mob *mob)
		{
			this -> mob = mob;
		}

		inline void setSpellID(int spell_id)
		{
			this -> spell_id = spell_id;
		}

		inline void setBuffSlot(int buff_slot)
		{
			this -> buff_slot = buff_slot;
		}

		inline void setCasterID(int caster_id)
		{
			this -> caster_id = caster_id;
		}

	private:
		Mob *mob;
		int spell_id;
		int buff_slot;
		int caster_id;
};

class Event_Spell_Effect_Translocate_Complete : public Event
{
	public:
		Event_Spell_Effect_Translocate_Complete();
		int Post();
		void ExportAdditionalVariables();

		inline void setSpellID(int spell_id)
		{
			this -> spell_id = spell_id;
		}

		std::string Event_Name_UC()   	{return "EVENT_SPELL_EFFECT_TRANSLOCATE_COMPLETE";}
		std::string Event_Name_LC()		{return "event_spell_effect_translocate_complete";}

	private:
		int spell_id;
};

// See Object::HandleCombine and Object::HandleAutoCombine
// Called when a tradeskill combine succeeds
class Event_Combine_Success : public Event
{
	public:
		Event_Combine_Success();
		int Post();
		void ExportAdditionalVariables();

		inline void setRecipeID(int recipe_id)
		{
			this -> recipe_id = recipe_id;
		}

		inline void setRecipeName(std::string recipe_name)
		{
			this -> recipe_name = recipe_name;
		}


		std::string Event_Name_UC()   	{return "EVENT_COMBINE_SUCCESS";}
		std::string Event_Name_LC()		{return "event_combine_success";}

	private:
		int recipe_id;
		std::string recipe_name;
};

// See Object::HandleCombine and Object::HandleAutoCombine
// Called when a tradeskill combine fails
class Event_Combine_Failure : public Event
{
	public:
		Event_Combine_Failure();
		int Post();
		void ExportAdditionalVariables();

		inline void setRecipeID(int recipe_id)
		{
			this -> recipe_id = recipe_id;
		}

		inline void setRecipeName(std::string recipe_name)
		{
			this -> recipe_name = recipe_name;
		}


		std::string Event_Name_UC()   	{return "EVENT_COMBINE_FAILURE";}
		std::string Event_Name_LC()		{return "event_combine_failure";}

	private:
		int recipe_id;
		std::string recipe_name;
};

// See Client::Handle_OP_ItemVerifyRequest

class Event_Item_Click : public Event
{
	public:
		Event_Item_Click();
		int Post();
		void ExportAdditionalVariables();

		inline void setItem(EQ::ItemInstance *item_inst)
		{
			this -> item_inst = item_inst;
		}

		inline void setSlotID(int slot_id)
		{
			this -> slot_id = slot_id;
		}

		std::string Event_Name_UC()   	{return "EVENT_ITEM_CLICK";}
		std::string Event_Name_LC()		{return "event_item_click";}

	private:
		EQ::ItemInstance *item_inst;
		int slot_id;
};

// See Client::Handle_OP_CastSpell, and Client::Handle_OP_ItemVerifyRequest
class Event_Item_Click_Cast : public Event
{
	public:
		Event_Item_Click_Cast();
		int Post();
		void ExportAdditionalVariables();

		inline void setItem(EQ::ItemInstance *item_inst)
		{
			this -> item_inst = item_inst;
		}

		inline void setSlotID(int slot_id)
		{
			this -> slot_id = slot_id;
		}


		std::string Event_Name_UC()   	{return "EVENT_ITEM_CLICK_CAST";}
		std::string Event_Name_LC()		{return "event_item_click_cast";}

	private:
		EQ::ItemInstance *item_inst;
		int slot_id;
};

// See Mob::SetGrouped and Mob::SetRaidGrouped
//
// The list of current group/raid members in this zone has changed
//
// Called when a client joins a group or raid, or zones in with a group/raid already in place
// 

class Event_Group_Change : public Event
{
	public:
		Event_Group_Change();
		int Post();
		void ExportAdditionalVariables();

		std::string Event_Name_UC()   	{return "EVENT_GROUP_CHANGE";}
		std::string Event_Name_LC()		{return "event_group_change";}
};

// See Client::ForageItem -- called when the client successfully forages an item

class Event_Forage_Success : public Event
{
	public:
		Event_Forage_Success();
		int Post();
		void ExportAdditionalVariables();

		inline void setItem(EQ::ItemInstance *item_inst)
		{
			this -> item_inst = item_inst;
		}

		std::string Event_Name_UC()   	{return "EVENT_FORAGE_SUCCESS";}
		std::string Event_Name_LC()		{return "event_forage_success";}

	private:
		EQ::ItemInstance *item_inst;					// item foraged
};

class Event_Forage_Failure : public Event
{
	public:
		Event_Forage_Failure();
		int Post();
		void ExportAdditionalVariables();

		std::string Event_Name_UC()   	{return "EVENT_FORAGE_FAILURE";}
		std::string Event_Name_LC()		{return "event_forage_failure";}
};

class Event_Fish_Start : public Event
{
	public:
		Event_Fish_Start();
		int Post();
		void ExportAdditionalVariables();

		std::string Event_Name_UC()   		{return "EVENT_FISH_START";}
		std::string Event_Name_LC()		{return "event_fish_start";}
};

class Event_Fish_Success : public Event
{
	public:
		Event_Fish_Success();
		int Post();
		void ExportAdditionalVariables();

		inline void setItem(EQ::ItemInstance *item)
		{
			this -> item_inst = item;
		}

		std::string Event_Name_UC()   	{return "EVENT_FISH_SUCCESS";}
		std::string Event_Name_LC()		{return "event_fish_success";}

	private:
		EQ::ItemInstance *item_inst;					// the item obtained
};

// See Client::GoFish -- called when the client fails to obtain anything via fishing
class Event_Fish_Failure : public Event
{
	public:
		Event_Fish_Failure();
		int Post();
		void ExportAdditionalVariables();

		std::string Event_Name_UC()   		{return "EVENT_FISH_FAILURE";}
		std::string Event_Name_LC()		{return "event_fish_failure";}
};

// See Client::Handle_OP_ClickObject
class Event_Click_Object : public Event
{
	public:
		Event_Click_Object();
		int Post();
		void ExportAdditionalVariables();

		inline void setObject(ClickObject_Struct *object)
		{
			this -> object = object;
		}

		std::string Event_Name_UC()   		{return "EVENT_CLICK_OBJECT";}
		std::string Event_Name_LC()		{return "event_click_object";}

	private:
		ClickObject_Struct *object;
};

// See Client::DiscoverItem
// Called when the Client discovers an item (acquires an item not in the discovery table)
// Discoveries are server-wide, so only the first person to acquire the item gets credit

class Event_Discover_Item : public Event
{
	public:
		Event_Discover_Item();
		int Post();
		void ExportAdditionalVariables();

		inline void setItemID(uint32 item_id)
		{
			this -> item_id = item_id;
		}

		std::string Event_Name_UC()   		{return "EVENT_DISCOVER_ITEM";}
		std::string Event_Name_LC()		{return "event_discover_item";}

	private:
		uint32 item_id;
};

class Event_Disconnect : public Event
{
	public:
		Event_Disconnect();
		int Post();
		void ExportAdditionalVariables();

		std::string Event_Name_UC()   	{return "EVENT_DISCONNECT";}
		std::string Event_Name_LC()		{return "event_disconnect";}
};

class Event_Connect : public Event
{
	public:
		Event_Connect();
		int Post();
		void ExportAdditionalVariables();

		std::string Event_Name_UC()   	{return "EVENT_CONNECT";}
		std::string Event_Name_LC()		{return "event_connect";}
};

class Event_Item_Tick : public Event
{
	public:
		Event_Item_Tick();
		int Post();
		void ExportAdditionalVariables();

		inline void setItem(EQ::ItemInstance *item_inst)
		{
			this -> item_inst = item_inst;
		}

		inline void setSlot(int slot_id)
		{
			this -> slot_id = slot_id;
		}

		std::string Event_Name_UC()   		{return "EVENT_ITEM_TICK";}
		std::string Event_Name_LC()		{return "event_item_tick";}

	private:
		EQ::ItemInstance *item_inst;
		int slot_id;
	
};

class Event_Duel_Win : public Event
{
	public:
		Event_Duel_Win();
		int Post();
		void ExportAdditionalVariables();

		inline void setWinner(Mob *winner)
		{
			this -> winner = winner;
		}

		inline void setLoser(Mob *loser)
		{
			this -> loser = loser;
		}

	private:
		Mob *winner;
		Mob *loser;
};

class Event_Duel_Lose : public Event
{
	public:
		Event_Duel_Lose();
		int Post();
		void ExportAdditionalVariables();

		inline void setWinner(Mob *winner)
		{
			this -> winner = winner;
		}

		inline void setLoser(Mob *loser)
		{
			this -> loser = loser;
		}

	private:
		Mob *winner;
		Mob *loser;
};

class Event_Command : public Event
{
	public:
		Event_Command();
		int Post();
		void ExportAdditionalVariables();

		inline void setText(std::string text)
		{
			this -> text = text;
		}

		std::string Event_Name_UC()   	{return "EVENT_COMMAND";}
		std::string Event_Name_LC()		{return "event_command";}

	private:
		std::string text;
};

// See Client::HandleRespawnFromHover -- called when a client respawns after death

class Event_Respawn : public Event
{
	public:
		enum Respawn_Type {Respawn_Rez, Respawn_Norez};

		Event_Respawn();
		int Post();
		void ExportAdditionalVariables();

		inline void setRespawnType(Respawn_Type type)
		{
			this -> type = type;
		}

		inline void setRespawnOption(int option)
		{
			this -> option = option;
		}

		std::string Event_Name_UC()   	{return "EVENT_RESPAWN";}
		std::string Event_Name_LC()		{return "event_respawn";}

	private:
		Respawn_Type type;
		int option;								// selected bindpoint option, think 0 is default
};

class Event_Death : public Event
{
	public:
		Event_Death();
		int Post();
		void ExportAdditionalVariables();

		inline void setDeadMob(Mob *dead_mob)
		{
			this -> dead_mob = dead_mob;
		}
		int getDeadMobID();

		inline void setKiller(Mob *killer)
		{
			this -> dead_mob = dead_mob;
		}
		int getKillerID();

		inline void setTopHate(Mob *top_of_hate)
		{
			this -> top_of_hate = top_of_hate;
		}
		inline Mob *getTopOfHate()		{ return top_of_hate;}
		inline void setDamage(int32 damage)
		{
			this -> damage = damage;
		}

		inline void setSpellID(int32 spell_id)
		{
			this -> spell_id = spell_id;
		}

		inline void setAttackSkill(EQ::skills::SkillType attack_skill)
		{
			this -> attack_skill = static_cast<int>(attack_skill);
		}

		inline void setSpawnGroup(int32 spawn_group)
		{
			this -> spawn_group = spawn_group;
		}
		inline int32 getSpawnGroup()
		{
			return spawn_group;
		}

		std::string Event_Name_UC()   	{return "EVENT_DEATH";}
		std::string Event_Name_LC()		{return "event_death";}

	private:
		Mob *dead_mob;							// dead mob
		Mob *killer;							// credited with kill (may be nullptr)
		Mob *top_of_hate;						// client at the top of the hate list, could be nullptr
		int32 damage;							// amount of damage on kill-shot
		uint16 spell_id;						// spell-id, if applicable
		int attack_skill;						// skill used to kill the mob
		
		int32 spawn_group;						// sometimes we only want to know what SG the mob was in
};

// similar to Event_Death, called when death handling is finished
class Event_Death_Complete : public Event
{
	public:
		Event_Death_Complete();
		int Post();
		void ExportAdditionalVariables();

		inline void setDeadMob(Mob *dead_mob)
		{
			this -> dead_mob = dead_mob;
		}

		inline void setKiller(Mob *killer)
		{
			this -> killer = killer;
		}

		inline void setDamage(int32 damage)
		{
			this -> damage = damage;
		}

		inline void setSpellID(int16 spell_id)
		{
			this -> spell_id = spell_id;
		}

		inline void setAttackSkill(EQ::skills::SkillType attack_skill)
		{
			this -> attack_skill = static_cast<int>(attack_skill);
		}

		std::string Event_Name_UC()   	{return "EVENT_DEATH";}
		std::string Event_Name_LC()		{return "event_death";}

	private:
		Mob *dead_mob;
		Mob *killer;

		int32 damage;							// amount of damage on kill-shot
		uint16 spell_id;						// spell-id, if applicable
		int attack_skill;						// skill used to kill the mob
};

class Event_Proximity : public Event
{
	public:
		Event_Proximity();
		int Post();
		void ExportAdditionalVariables();

		inline void setZone(int zone)
		{
			this -> zone = zone;
		}
		inline int getZone()		{return zone;}

		inline void setXYZ(float x, float y, float z)
		{
			this -> x = x;
			this -> y = y;
			this -> z = z;
		}
		inline float getX()			{return x;}
		inline float getY()			{return y;}
		inline float getZ()			{return z;}

		inline void setDescriptor(EntityType type, int32 entityID)
		{
			this -> entity.type = type;
			this -> entity.entityID = entityID;
		}

		std::string Event_Name_UC()   		{return "EVENT_PROXIMITY";}
		std::string Event_Name_LC()		{return "event_proximity";}
	private:
		EntityDescriptor entity;		// the type and ID
		int zone;
		float x;
		float y;
		float z;
};

class Event_Item : public Event
{
	public:
		Event_Item();
		int Post();
		void ExportAdditionalVariables();

		inline void setItemID(uint32 itemID)
		{
			this -> itemID = itemID;
		}
		inline int32 getItemID()	{return itemID;}

		inline void setCount(int32 count)
		{
			this -> count = count;
			if (this -> count == -1)
			{
				this -> count = 1;
			}
		}
		inline int32 getCount()	{return count;}

		inline void setReceiver(EntityType type, int32 entityID)
		{
			this -> receiver.type = type;
			this -> receiver.entityID = entityID;
		}
		inline EntityType getReceiverType()	{return receiver.type;}
		inline int32 getReceiverID()		{return receiver.entityID;}
		uint32 getCharacterID();

		std::string Event_Name_UC()   		{return "EVENT_ITEM";}
		std::string Event_Name_LC()			{return "event_item";}

	private:
		EntityDescriptor receiver;
		int32 itemID;
		int32 count;
};

class Event_Drop : public Event
{
	public:
		Event_Drop();
		int Post();
		void ExportAdditionalVariables();

		inline void setCount(int32 count)
		{
			this -> count = count;
		}
		inline int32 getCount()	{return count;}

		inline void setItemID(uint32 itemID)
		{
			this -> itemID = itemID;
		}
		inline int32 getItemID()	{return itemID;}
		inline void setInst(EQ::ItemInstance *inst)
		{
			this -> inst = inst;
		}
		inline EQ::ItemInstance* getInst()	{return inst;}

		void setXYZ(float x, float y, float z);
		inline float getX()		{return x;}
		inline float getY()		{return y;}
		inline float getZ()		{return z;}

		std::string Event_Name_UC()   	{return "EVENT_DROP";}
		std::string Event_Name_LC()		{return "event_drop";}
	private:
		EQ::ItemInstance *inst;			// the instance dropped
		int32 itemID;
		int32 count;

		float x;				// object coordinates
		float y;
		float z;
};

class Event_Damage : public Event
{
	private:
		EntityDescriptor target;			// I suppose this could be used for items/locations in an extreme case
		int percent;						// amount of damage in %

	public:
		Event_Damage();
		int Post();
		void ExportAdditionalVariables();

		inline void setTarget(EntityDescriptor target)
		{
			this -> target = target;
		}
		inline EntityDescriptor getTarget()		{return target;}

		inline void setPercent(int percent)
		{
			this -> percent = percent;
		}
		inline int getPercent()		{return percent;}
};

class Event_ItemUse : public Event
{
	private:
		int item_id;
		int target_eid;


	public:
		Event_ItemUse();
		int Post();
		void ExportAdditionalVariables();

		inline void setItem(int item_id)
		{
			this -> item_id = item_id;
		}
		inline int getItem()			{return item_id;}

		// 3/23/2015:   how is EID supposed to be used?
		// I believe EID is a client-side index

		inline void setEID(int target_eid)
		{
			this -> target_eid = target_eid;
		}
		inline int getEID()				{return target_eid;}
};

#endif


