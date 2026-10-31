// Actions.cpp : see Actions.h.
#include "stdafx.h"
#include "Actions.h"
#include "../World.h"
#include "../Log.h"
#include "../../../core/coop_core.h"

#include <map>
#include <set>
#include <string>

namespace
{
	// [117][kind u8][who u32] then by kind:
	//   ability: [ability type u32]               effect start: [effect id u32][pool id u32] (pool 1 or 2 by kind)
	//   effect stop: [pool id u32]                voice: [p2 i32][p3 i32][len u8][name bytes]
	const uint8_t kMsgAction = 117;
	enum Kind : uint8_t { kAbility = 1, kEffect1 = 2, kEffect2 = 3, kEffectStop = 4, kVoice = 5 };

	const DWORD kRepeatMs = 300;        // the same thing of the same creature is sent at most this often
	const DWORD kEffectMaxMs = 8000;    // a replayed effect the other side never stopped is stopped after this long
	const DWORD kAbilityHoldMs = 3000;  // a creature that plays an ability is left standing this long (PuppetMotion::Hold)

	int sDepth = 0;         // nesting of our capture detours: only the outermost call is captured
	int sAbilityDepth = 0;  // inside a non-attack ability of a known creature
	int sReplaying = 0;
	bool sLogging = false;
	Simulator::cCreatureBase* sEchoTarget = nullptr;

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

	void Note(uint8_t kind, uint32_t who, uint32_t id, const char* text)
	{
		int& n = S().seen[Key(kind, id)];
		if (sLogging || ++n <= 3) {
			Logf("action: %s %08x %s by %s %u%s", KindName(kind), id, text ? text : "", IsAvatar(who) ? "avatar" : "animal", who,
				sLogging ? "" : " (first times only)");
		}
	}

	// Runs `apply` on our copy of `who`: the creature of the other side, or the echo puppet for our own avatar.
	template <typename F>
	void Replay(uint32_t who, Simulator::cCreatureBase* target, F apply)
	{
		if (!target) {
			return;
		}
		++sReplaying;
		apply(target);
		--sReplaying;
	}

	Simulator::cCreatureBase* CopyOf(uint32_t who)
	{
		return static_cast<Simulator::cCreatureBase*>(static_cast<Simulator::cCreatureAnimal*>(World::CreatureOf(who)));
	}

	void PlayAbilityOf(Simulator::cCreatureBase* target, uint32_t type, uint32_t who)
	{
		int index = target->GetAbilityIndexByType((int)type);
		if (index < 0) {
			Logf("action: ability type %u is not known to our copy of %u, not played", type, who);
			return;
		}
		target->PlayAbility(index);
	}

	void StartEffectOf(Simulator::cCreatureBase* target, uint8_t kind, uint32_t effect, uint32_t pool, uint32_t who)
	{
		Swarm::IVisualEffect* fx = nullptr;
		if (kind == kEffect1) {
			fx = target->CreateAndStartEffectForPool1Renamed(effect, pool);
		}
		else {
			fx = target->CreateEffectForPool2Renamed(effect, pool);
			if (fx) {
				fx->Start(0);
			}
		}
		if (fx) {
			S().replayed[Key(who, pool)] = { who, pool, GetTickCount() };
		}
	}

	// Sends one captured thing (and plays it on the echo puppet). False when it is not sent (unknown creature, too soon).
	bool Capture(Simulator::cCreatureBase* creature, uint8_t kind, uint32_t id, uint32_t pool, const char* voice, int p2, int p3)
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
		if (!known) {
			who = 0;
		}
		// effects and voices of animals would flood the link (every step of every animal): only the avatars' are sent
		if (kind != kAbility && !IsAvatar(who)) {
			return false;
		}
		State& s = S();
		DWORD now = GetTickCount();
		if (kind != kEffectStop) {
			DWORD& last = s.lastSent[Key(((uint32_t)kind << 24) ^ who, id)];
			if (now - last < kRepeatMs) {
				return false;
			}
			last = now;
		}
		else if (!s.startedPools.erase(Key(who, id))) {
			return false;  // a stop of something we never sent
		}
		if (kind == kEffect1 || kind == kEffect2) {
			s.startedPools.insert(Key(who, pool));
		}
		Note(kind, who, id, voice);

		if (known) {
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
				w.U32(id);
				if (kind == kEffect1 || kind == kEffect2) {
					w.U32(pool);
				}
			}
			World::SendReliable(w.buf.data(), w.buf.size());
		}
		if (echo) {
			Replay(who, echo, [&](Simulator::cCreatureBase* t) {
				switch (kind) {
				case kAbility: PlayAbilityOf(t, id, who); break;
				case kEffect1: case kEffect2: StartEffectOf(t, kind, id, pool, who); break;
				case kEffectStop: t->StopEffectFromPools(id, 0); S().replayed.erase(Key(who, id)); break;
				case kVoice: t->PlayVoice(voice, p2, p3); break;
				}
			});
		}
		return true;
	}

	// An attack ability (bite, strike, charge, spit) is left to PlayAnimation and the damage messages, as before.
	bool IsAttack(Simulator::cCreatureBase* creature, int index)
	{
		Simulator::cCreatureAbility* ability = index >= 0 ? creature->GetAbility(index) : nullptr;
		return !ability || ability->mCategory == Simulator::cCreatureAbility::kCategoryAttack;
	}
}

member_detour(PlayAbility_detour, Simulator::cCreatureBase, void(int, Anim::AnimIndex*))
{
	void detoured(int abilityIndex, Anim::AnimIndex* dstAnimIndex)
	{
		bool outer = sDepth == 0 && !sReplaying && !IsAttack(this, abilityIndex);
		bool sent = false;
		if (outer) {
			Simulator::cCreatureAbility* ability = GetAbility(abilityIndex);
			sent = Capture(this, kAbility, (uint32_t)ability->mType, 0, nullptr, 0, 0);
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

	bool OnMessage(const uint8_t* data, uint32_t size)
	{
		if (size < 6 || data[0] != kMsgAction) {
			return false;
		}
		coop::Reader r(data, size);
		r.U8();
		uint8_t kind = r.U8();
		uint32_t who = r.U32();
		Simulator::cCreatureBase* target = CopyOf(who);
		if (kind == kVoice) {
			int p2 = (int)r.U32(), p3 = (int)r.U32();
			uint8_t len = r.U8();
			char name[256] = {};
			for (uint8_t i = 0; i < len; ++i) {
				name[i] = (char)r.U8();
			}
			if (r.ok) {
				if (sLogging) {
					Logf("action: replay voice %s on %u (%s)", name, who, target ? "ok" : "no copy");
				}
				Replay(who, target, [&](Simulator::cCreatureBase* t) { t->PlayVoice(name, p2, p3); });
			}
			return true;
		}
		uint32_t id = r.U32();
		uint32_t pool = (kind == kEffect1 || kind == kEffect2) ? r.U32() : 0;
		if (!r.ok) {
			return true;
		}
		if (sLogging) {
			Logf("action: replay %s %08x pool %08x on %u (%s)", KindName(kind), id, pool, who, target ? "ok" : "no copy");
		}
		Replay(who, target, [&](Simulator::cCreatureBase* t) {
			switch (kind) {
			case kAbility:
				PlayAbilityOf(t, id, who);
				World::HoldCreature(who, kAbilityHoldMs);
				break;
			case kEffect1: case kEffect2:
				StartEffectOf(t, kind, id, pool, who);
				break;
			case kEffectStop:
				t->StopEffectFromPools(id, 0);
				S().replayed.erase(Key(who, id));
				break;
			}
		});
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
			Simulator::cCreatureBase* target = sEchoTarget && IsAvatar(it->second.who) ? sEchoTarget : CopyOf(it->second.who);
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

	void SetLogging(bool on) { sLogging = on; }
	bool GetLogging() { return sLogging; }
}
