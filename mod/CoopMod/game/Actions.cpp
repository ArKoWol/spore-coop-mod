// Actions.cpp : see Actions.h.
#include "stdafx.h"
#include "Actions.h"
#include "PuppetMotion.h"
#include "../World.h"
#include "../Log.h"
#include "../../../core/coop_core.h"

#include <map>
#include <set>
#include <string>
#include <vector>

namespace
{
	// [117][kind u8][who u32] then by kind:
	//   ability: [ability type u32]               effect start: [effect id u32][pool id u32] (pool 1 or 2 by kind)
	//   effect stop: [pool id u32]                voice: [p2 i32][p3 i32][len u8][name bytes]
	const uint8_t kMsgAction = 117;
	enum Kind : uint8_t { kAbility = 1, kEffect1 = 2, kEffect2 = 3, kEffectStop = 4, kVoice = 5 };

	const DWORD kRepeatMs = 300;        // the same thing of the same creature is sent at most this often
	const DWORD kAnimRepeatMs = 400;    // the same for plain animations on the echo puppet (as World's kMsgAnim)
	const DWORD kEffectMaxMs = 8000;    // a replayed effect the other side never stopped is stopped after this long
	const DWORD kAbilityHoldMs = 3000;  // a creature that plays an ability is left standing this long (PuppetMotion::Hold)
	const DWORD kAnimHoldMs = 2500;

	int sDepth = 0;         // nesting of our capture detours: only the outermost call is captured
	int sAbilityDepth = 0;  // inside a non-attack ability of a known creature
	int sReplaying = 0;
	bool sLogging = false;
	Simulator::cCreatureBase* sEchoTarget = nullptr;
	Actions::Counters sCounters;

	struct Replayed { uint32_t who; uint32_t pool; DWORD since; };

	// Everything with a destructor lives in one heap object, freed in Detach (no static destructors at process exit).
	struct State
	{
		std::map<uint64_t, DWORD> lastSent;     // (kind, who, id) -> tick
		std::map<uint64_t, int> seen;           // (kind, id) -> how often captured, for the log
		std::set<uint64_t> startedPools;        // (who, pool) we sent a start for: only their stops are sent
		std::map<uint64_t, Replayed> replayed;  // (who, pool) -> a replayed effect still running here
	};
	State* sState = nullptr;

	State& S()
	{
		if (!sState) {
			sState = new State();
		}
		return *sState;
	}

	uint64_t Key(uint32_t a, uint32_t b) { return ((uint64_t)a << 32) | b; }

	const char* KindName(uint8_t kind)
	{
		switch (kind) {
		case kAbility: return "ability";
		case kEffect1: return "effect";
		case kEffect2: return "effect2";
		case kEffectStop: return "stop";
		case kVoice: return "voice";
		}
		return "?";
	}

	bool IsAvatar(uint32_t who) { return who == 0 || who == 0xFFFFFFFFu; }

	bool LogFirst(uint32_t kind, uint32_t id)
	{
		return sLogging || ++S().seen[Key(kind, id)] <= 3;
	}

	Simulator::cCreatureBase* CopyOf(uint32_t who)
	{
		return static_cast<Simulator::cCreatureBase*>(static_cast<Simulator::cCreatureAnimal*>(World::CreatureOf(who)));
	}

	// The type and category of ability `index` of `creature`; false when there is none (an index past the list may read garbage).
	bool ReadAbility(Simulator::cCreatureBase* creature, int index, int* type, int* category)
	{
		__try {
			Simulator::cCreatureAbility* ability = creature->GetAbility(index);
			if (!ability) {
				return false;
			}
			*type = (int)ability->mType;
			*category = ability->mCategory;
			return *type > 0 && *type < 200;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			return false;
		}
	}

	bool ReadWords(const void* data, uint32_t* out, int n)
	{
		__try {
			for (int i = 0; i < n; ++i) {
				out[i] = ((const uint32_t*)data)[i];
			}
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			return false;
		}
	}

	// An attack ability (bite, strike, charge, spit) is left to PlayAnimation and the damage messages, as before.
	bool IsAttack(Simulator::cCreatureBase* creature, int index)
	{
		int type = 0, category = 0;
		return !ReadAbility(creature, index, &type, &category) || category == Simulator::cCreatureAbility::kCategoryAttack;
	}

	void Hold(uint32_t who, bool echo, unsigned long ms)
	{
		if (echo) {
			PuppetMotion::HoldPuppet(ms);
		}
		else {
			World::HoldCreature(who, ms);
		}
	}

	// Plays one kMsgAction on `target` (our copy of its creature). True when the game took it (the ability is known to the copy,
	// the effect was made); `echo` = the target is the echo puppet of our own avatar.
	bool Apply(const uint8_t* data, size_t size, Simulator::cCreatureBase* target, bool echo)
	{
		coop::Reader r(data, size);
		r.U8();
		uint8_t kind = r.U8();
		uint32_t who = r.U32();
		if (kind == kVoice) {
			int p2 = (int)r.U32(), p3 = (int)r.U32();
			uint8_t len = r.U8();
			char name[256] = {};
			for (uint8_t i = 0; i < len; ++i) {
				name[i] = (char)r.U8();
			}
			if (!r.ok) {
				return false;
			}
			if (LogFirst(0x80 | kVoice, (uint32_t)id(name))) {
				Logf("action: replay voice %s on %s %u (%s)", name, echo ? "echo" : "copy of", who, target ? "ok" : "no copy");
			}
			if (!target) {
				return false;
			}
			++sReplaying;
			target->PlayVoice(name, p2, p3);
			--sReplaying;
			++sCounters.replayed;
			return true;
		}
		uint32_t value = r.U32();
		uint32_t pool = (kind == kEffect1 || kind == kEffect2) ? r.U32() : 0;
		if (!r.ok) {
			return false;
		}
		bool ok = false;
		if (target) {
			++sReplaying;
			switch (kind) {
			case kAbility: {
				int index = target->GetAbilityIndexByType((int)value);
				if (index >= 0) {
					target->PlayAbility(index);
					Hold(who, echo, kAbilityHoldMs);
					ok = true;
				}
				break;
			}
			case kEffect1: case kEffect2: {
				Swarm::IVisualEffect* fx = nullptr;
				if (kind == kEffect1) {
					fx = target->CreateAndStartEffectForPool1Renamed(value, pool);
				}
				else if ((fx = target->CreateEffectForPool2Renamed(value, pool)) != nullptr) {
					fx->Start(0);
				}
				if (fx) {
					S().replayed[Key(echo ? 0xEC40u : who, pool)] = { who, pool, GetTickCount() };
					ok = true;
				}
				break;
			}
			case kEffectStop:
				target->StopEffectFromPools(value, 0);
				S().replayed.erase(Key(echo ? 0xEC40u : who, value));
				ok = true;
				break;
			}
			--sReplaying;
		}
		if (ok) {
			++sCounters.replayed;
		}
		if (LogFirst(0x80 | kind, value)) {
			Logf("action: replay %s %08x pool %08x on %s %u (%s)", KindName(kind), value, pool, echo ? "echo" : "copy of", who,
				!target ? "no copy" : ok ? "ok" : "not taken by the game");
		}
		return ok;
	}

	// Sends one captured thing to the other side, and through the same bytes to the echo puppet. False when it is not sent.
	bool Capture(Simulator::cCreatureBase* creature, uint8_t kind, uint32_t value, uint32_t pool, const char* voice, int p2, int p3)
	{
		if (sReplaying || !creature) {
			return false;
		}
		uint32_t who = 0;
		auto avatar = GameNounManager.GetAvatar();
		bool ownAvatar = avatar && creature == static_cast<Simulator::cCreatureBase*>(avatar);
		bool known = World::WhoOf(creature, &who);
		Simulator::cCreatureBase* echo = ownAvatar ? sEchoTarget : nullptr;
		if (!known && !echo) {
			return false;
		}
		// effects and voices of animals would flood the link (every step of every animal): only the avatars' are sent
		if (kind != kAbility && known && !IsAvatar(who)) {
			return false;
		}
		State& s = S();
		DWORD now = GetTickCount();
		if (kind != kEffectStop) {
			DWORD& last = s.lastSent[Key(((uint32_t)kind << 24) ^ who, value)];
			if (now - last < kRepeatMs) {
				return false;
			}
			last = now;
		}
		else if (!s.startedPools.erase(Key(who, value))) {
			return false;  // a stop of something we never sent
		}
		if (kind == kEffect1 || kind == kEffect2) {
			s.startedPools.insert(Key(who, pool));
		}
		switch (kind) {
		case kAbility: ++sCounters.abilities; break;
		case kEffect1: case kEffect2: ++sCounters.effects; break;
		case kEffectStop: ++sCounters.stops; break;
		case kVoice: ++sCounters.voices; break;
		}
		if (LogFirst(kind, value)) {
			Logf("action: %s %08x %s by %s %u%s", KindName(kind), value, voice ? voice : "", IsAvatar(who) ? "avatar" : "animal", who,
				sLogging ? "" : " (first times only)");
		}

		coop::Writer w;
		w.U8(kMsgAction); w.U8(kind); w.U32(who);
		if (kind == kVoice) {
			size_t len = voice ? strlen(voice) : 0;
			len = len > 255 ? 255 : len;
			w.U32((uint32_t)p2); w.U32((uint32_t)p3); w.U8((uint8_t)len);
			for (size_t i = 0; i < len; ++i) {
				w.U8((uint8_t)voice[i]);
			}
		}
		else {
			w.U32(value);
			if (kind == kEffect1 || kind == kEffect2) {
				w.U32(pool);
			}
		}
		if (known) {
			World::SendReliable(w.buf.data(), w.buf.size());
		}
		if (echo) {
			Apply(w.buf.data(), w.buf.size(), echo, true);
		}
		return true;
	}
}

member_detour(PlayAbility_detour, Simulator::cCreatureBase, void(int, Anim::AnimIndex*))
{
	void detoured(int abilityIndex, Anim::AnimIndex* dstAnimIndex)
	{
		bool sent = false;
		int type = 0, category = 0;
		if (sDepth == 0 && !sReplaying && !IsAttack(this, abilityIndex) && ReadAbility(this, abilityIndex, &type, &category)) {
			sent = Capture(this, kAbility, (uint32_t)type, 0, nullptr, 0, 0);
		}
		++sDepth;
		if (sent) {
			++sAbilityDepth;
		}
		original_function(this, abilityIndex, dstAnimIndex);
		if (sent) {
			--sAbilityDepth;
		}
		--sDepth;
	}
};

member_detour(EffectPool1_detour, Simulator::cCreatureBase, Swarm::IVisualEffect*(uint32_t))
{
	Swarm::IVisualEffect* detoured(uint32_t effectId)
	{
		if (sDepth == 0 && sAbilityDepth == 0) {
			Capture(this, kEffect1, effectId, effectId, nullptr, 0, 0);
		}
		++sDepth;
		Swarm::IVisualEffect* fx = original_function(this, effectId);
		--sDepth;
		return fx;
	}
};

member_detour(EffectPool1Renamed_detour, Simulator::cCreatureBase, Swarm::IVisualEffect*(uint32_t, uint32_t))
{
	Swarm::IVisualEffect* detoured(uint32_t effectId, uint32_t poolId)
	{
		if (sDepth == 0 && sAbilityDepth == 0) {
			Capture(this, kEffect1, effectId, poolId, nullptr, 0, 0);
		}
		++sDepth;
		Swarm::IVisualEffect* fx = original_function(this, effectId, poolId);
		--sDepth;
		return fx;
	}
};

member_detour(EffectPool2Renamed_detour, Simulator::cCreatureBase, Swarm::IVisualEffect*(uint32_t, uint32_t))
{
	Swarm::IVisualEffect* detoured(uint32_t effectId, uint32_t poolId)
	{
		if (sDepth == 0 && sAbilityDepth == 0) {
			Capture(this, kEffect2, effectId, poolId, nullptr, 0, 0);
		}
		++sDepth;
		Swarm::IVisualEffect* fx = original_function(this, effectId, poolId);
		--sDepth;
		return fx;
	}
};

member_detour(EffectStartStop1_detour, Simulator::cCreatureBase, Swarm::IVisualEffect*(bool, uint32_t, uint32_t))
{
	Swarm::IVisualEffect* detoured(bool create, uint32_t effectId, uint32_t poolId)
	{
		if (sDepth == 0 && sAbilityDepth == 0) {
			Capture(this, create ? kEffect1 : kEffectStop, create ? effectId : poolId, poolId, nullptr, 0, 0);
		}
		++sDepth;
		Swarm::IVisualEffect* fx = original_function(this, create, effectId, poolId);
		--sDepth;
		return fx;
	}
};

member_detour(EffectStartStop2_detour, Simulator::cCreatureBase, Swarm::IVisualEffect*(bool, uint32_t, uint32_t))
{
	Swarm::IVisualEffect* detoured(bool create, uint32_t effectId, uint32_t poolId)
	{
		if (sDepth == 0 && sAbilityDepth == 0) {
			Capture(this, create ? kEffect2 : kEffectStop, create ? effectId : poolId, poolId, nullptr, 0, 0);
		}
		++sDepth;
		Swarm::IVisualEffect* fx = original_function(this, create, effectId, poolId);
		--sDepth;
		return fx;
	}
};

member_detour(EffectStop_detour, Simulator::cCreatureBase, Swarm::IVisualEffect*(uint32_t, int))
{
	Swarm::IVisualEffect* detoured(uint32_t poolId, int hardStop)
	{
		if (sDepth == 0) {
			Capture(this, kEffectStop, poolId, poolId, nullptr, 0, 0);
		}
		++sDepth;
		Swarm::IVisualEffect* fx = original_function(this, poolId, hardStop);
		--sDepth;
		return fx;
	}
};

member_detour(PlayVoice_detour, Simulator::cCreatureBase, int(const char*, int, int))
{
	int detoured(const char* name, int param2, int param3)
	{
		if (sDepth == 0 && sAbilityDepth == 0 && name) {
			Capture(this, kVoice, (uint32_t)id(name), 0, name, param2, param3);
		}
		++sDepth;
		int result = original_function(this, name, param2, param3);
		--sDepth;
		return result;
	}
};

namespace Actions
{
	bool sAttached = false;

	void Attach()
	{
		sAttached = true;
		PlayAbility_detour::attach(GetAddress(Simulator::cCreatureBase, PlayAbility));
		EffectPool1_detour::attach(GetAddress(Simulator::cCreatureBase, CreateEffectForPool1));
		EffectPool1Renamed_detour::attach(GetAddress(Simulator::cCreatureBase, CreateEffectForPool1Renamed));
		EffectPool2Renamed_detour::attach(GetAddress(Simulator::cCreatureBase, CreateEffectForPool2Renamed));
		EffectStartStop1_detour::attach(GetAddress(Simulator::cCreatureBase, StartOrStopEffectFromPool1));
		EffectStartStop2_detour::attach(GetAddress(Simulator::cCreatureBase, StartOrStopEffectFromPool2));
		EffectStop_detour::attach(GetAddress(Simulator::cCreatureBase, StopEffectFromPools));
		PlayVoice_detour::attach(GetAddress(Simulator::cCreatureBase, PlayVoice));
	}

	void Detach()
	{
		if (sAttached) {
			sAttached = false;
			PlayAbility_detour::detach();
			EffectPool1_detour::detach();
			EffectPool1Renamed_detour::detach();
			EffectPool2Renamed_detour::detach();
			EffectStartStop1_detour::detach();
			EffectStartStop2_detour::detach();
			EffectStop_detour::detach();
			PlayVoice_detour::detach();
		}
		delete sState;
		sState = nullptr;
		sEchoTarget = nullptr;
	}

	void OnAnimation(void* creaturePtr, uint32_t animationId, int block, bool disableBlendIn)
	{
		auto avatar = GameNounManager.GetAvatar();
		if (sReplaying || !avatar || creaturePtr != (void*)static_cast<Simulator::cCreatureBase*>(avatar)) {
			return;
		}
		++sCounters.anims;
		// research: does the game play the player's actions in another block than the default (-1) we replay with?
		if (LogFirst(0xA1, animationId)) {
			Logf("action: animation %08x block %d blend-in off %d by our avatar", animationId, block, disableBlendIn ? 1 : 0);
		}
		// the echo puppet plays our avatar's animations as the other side would (World::OnMessage, kMsgAnim)
		if (!sEchoTarget || sAbilityDepth > 0) {
			return;
		}
		DWORD now = GetTickCount();
		DWORD& last = S().lastSent[Key(0xA0000000u, animationId)];
		if (now - last < kAnimRepeatMs) {
			return;
		}
		last = now;
		++sReplaying;
		sEchoTarget->PlayAnimation(animationId, block, disableBlendIn);
		--sReplaying;
		PuppetMotion::HoldPuppet(kAnimHoldMs);
		++sCounters.replayed;
	}

	void OnModeAction(uint32_t actionId, void* data)
	{
		++sCounters.modeActions;
		sCounters.lastModeAction = actionId;
		switch (actionId) {
		case 0xD3353638: case 0xD335362B: case 0xD335362D: case 0xD335362C: case 0xD3353636:
			return;  // Interact, EatFruit, EatMeat, Mate, Pickup: known (World::OnModeAction)
		}
		int& n = S().seen[Key(0xFF, actionId)];
		if (++n > 5 && !sLogging) {
			return;
		}
		uint32_t w[8] = {};
		if (!data || !ReadWords(data, w, 8)) {
			Logf("action: mode action %08x, data unreadable", actionId);
			return;
		}
		Logf("action: mode action %08x data %08x %08x %08x %08x %08x %08x %08x %08x", actionId, w[0], w[1], w[2], w[3], w[4], w[5],
			w[6], w[7]);
		// a small number among the words may be the index of one of our avatar's abilities
		auto avatar = GameNounManager.GetAvatar();
		if (!avatar) {
			return;
		}
		for (int i = 0; i < 8; ++i) {
			int type = 0, category = 0;
			if (w[i] < 32 && ReadAbility(avatar, (int)w[i], &type, &category)) {
				Logf("action:   word %d = %u: ability type %d, category %d", i, w[i], type, category);
			}
		}
	}

	bool OnMessage(const uint8_t* data, uint32_t size)
	{
		if (size < 6 || data[0] != kMsgAction) {
			return false;
		}
		uint32_t who = *(const uint32_t*)(data + 2);
		Apply(data, size, CopyOf(who), false);
		return true;
	}

	void Update()
	{
		if (!sState) {
			return;
		}
		DWORD now = GetTickCount();
		for (auto it = sState->replayed.begin(); it != sState->replayed.end();) {
			if (now - it->second.since < kEffectMaxMs) {
				++it;
				continue;
			}
			bool echo = (it->first >> 32) == 0xEC40u;
			Simulator::cCreatureBase* target = echo ? sEchoTarget : CopyOf(it->second.who);
			if (target) {
				++sReplaying;
				target->StopEffectFromPools(it->second.pool, 0);
				--sReplaying;
			}
			it = sState->replayed.erase(it);
		}
	}

	bool InAbility() { return sAbilityDepth > 0; }
	bool Replaying() { return sReplaying > 0; }

	void SetEchoTarget(void* creature)
	{
		sEchoTarget = creature ? static_cast<Simulator::cCreatureBase*>(static_cast<Simulator::cCreatureAnimal*>(creature)) : nullptr;
	}

	void* EchoTarget() { return sEchoTarget; }

	uint32_t CurrentAnimation(void* creaturePtr)
	{
		if (!creaturePtr) {
			return 0;
		}
		auto creature = static_cast<Simulator::cCreatureBase*>(static_cast<Simulator::cCreatureAnimal*>(creaturePtr));
		uint32_t current = 0;
		__try {
			if (Anim::AnimatedCreature* animated = creature->mpAnimatedCreature.get()) {
				animated->GetCurrentAnimation(&current);
			}
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			current = 0;
		}
		return current;
	}

	bool AbilityInfo(void* creature, int index, int* type, int* category)
	{
		return creature && ReadAbility(static_cast<Simulator::cCreatureBase*>(static_cast<Simulator::cCreatureAnimal*>(creature)),
			index, type, category);
	}

	Counters GetCounters() { return sCounters; }

	void SetLogging(bool on) { sLogging = on; }
	bool GetLogging() { return sLogging; }
}
