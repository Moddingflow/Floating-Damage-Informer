#include "FDN/Animation.h"
#include "FDN/DamageAggregator.h"
#include "FDN/FloatingNumberManager.h"
#include "FDN/Settings.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>

namespace
{
    int failures = 0;

    void Check(bool condition, std::string_view name)
    {
        if (!condition) {
            ++failures;
            std::cerr << "FAILED: " << name << '\n';
        }
    }

    bool Near(float lhs, float rhs, float epsilon = 0.001F)
    {
        return std::fabs(lhs - rhs) <= epsilon;
    }

    FDN::DamageEvent Event(
        FDN::ActorKey target,
        FDN::ActorKey source,
        float amount,
        double timestampMs,
        FDN::DamageType type = FDN::DamageType::Normal,
        bool playerTaken = false)
    {
        return FDN::DamageEvent{ target, source, amount, type, playerTaken, timestampMs };
    }

    FDN::DamageEvent EventAt(
        FDN::ActorKey target,
        FDN::ActorKey source,
        float amount,
        double timestampMs,
        FDN::WorldPoint impactPoint)
    {
        return FDN::DamageEvent{ target, source, amount, FDN::DamageType::Normal, false, timestampMs, impactPoint, true };
    }

    FDN::DamageContext Context(
        FDN::SourceRelation source,
        FDN::TargetRelation target,
        bool playerTaken = false)
    {
        return FDN::DamageContext{
            100,
            14,
            source,
            target,
            true,
            false,
            false,
            false,
            true,
            playerTaken
        };
    }

    void TestIniParsing()
    {
        FDN::Settings defaults;
        Check(Near(defaults.display.maxDistance, 18000.0F), "default max distance is extended");

        const auto generatedDefaults = FDN::SettingsManager::LoadFromString(FDN::SettingsManager::DefaultIni());
        Check(Near(generatedDefaults.display.maxDistance, 18000.0F), "default ini max distance is extended");

        const auto settings = FDN::SettingsManager::LoadFromString(R"(
[General]
bEnabled = false
bShowFollowerDealtDamage = yes

[Detection]
fMinDamageToShow = 2.5
bRoundDamage = false
iDecimalPlaces = 2

[Colors]
uNormalDamageColor = 0xCC112233
uMagicDamageColor = #112233 ; CSS-style color should not be parsed as a comment

[Font]
sFontName = SkyrimCustom # inline comment
)");

        Check(!settings.general.enabled, "bool false parses");
        Check(settings.general.showFollowerDealtDamage, "yes bool parses");
        Check(Near(settings.detection.minDamageToShow, 2.5F), "float parses");
        Check(!settings.detection.roundDamage, "round bool parses");
        Check(settings.detection.decimalPlaces == 2, "int parses");
        Check(settings.colors.normalDamageColor.ToARGB() == 0xCC112233, "argb color parses");
        Check(settings.colors.magicDamageColor.ToARGB() == 0xFF112233, "hash-prefixed rgb color parses in ini");
        Check(settings.font.fontName == "SkyrimCustom", "plain string field parses");
        Check(settings.FormatDamage(12.345F) == "12.35", "decimal damage formatting");
    }

    void TestMalformedIniAndClamping()
    {
        const auto settings = FDN::SettingsManager::LoadFromString(R"(
[General]
bEnabled = maybe

[Detection]
fMinDamageToShow = -99
fMergeWindowMs = -10
fHealthSampleDelayMs = 99999
iHealthSampleFrames = 999
fEventTimeoutMs = -1
fMagicSourceMemoryMs = 999999
iDecimalPlaces = 99

[Display]
fLifetime = -5
fRandomDriftPixels = -30
fMaxDistance = -1
        iMaxActiveNumbers = 99999
        iMaxNumbersPerActor = 99999
        fImpactPointVerticalOffset = 99999

[Font]
fFontSize = 999
sFontName = Skyrim – UTF8

[Animation]
fStartScale = -1
fEndScale = 99
fPopDurationMs = -5

[Colors]
fLargeDamageThreshold = 500
fCriticalDamageThreshold = 10
)");

        Check(settings.general.enabled, "bad bool falls back");
        Check(Near(settings.detection.minDamageToShow, 0.0F), "negative min damage clamps");
        Check(Near(settings.detection.mergeWindowMs, 0.0F), "negative merge window clamps");
        Check(Near(settings.detection.healthSampleDelayMs, 1000.0F), "sample delay upper clamps");
        Check(settings.detection.healthSampleFrames == 16, "sample frames upper clamps");
        Check(Near(settings.detection.eventTimeoutMs, 1.0F), "event timeout clamps");
        Check(Near(settings.detection.magicSourceMemoryMs, 60000.0F), "magic source memory upper clamps");
        Check(settings.detection.decimalPlaces == 4, "decimal places clamp");
        Check(Near(settings.display.lifetime, 0.1F), "lifetime lower clamps");
        Check(Near(settings.display.randomDriftPixels, 0.0F), "drift lower clamps");
        Check(Near(settings.display.maxDistance, 0.0F), "max distance lower clamps");
        Check(Near(settings.display.impactPointVerticalOffset, 3000.0F), "impact point offset upper clamps");
        Check(settings.display.maxActiveNumbers == 512, "max active upper clamps");
        Check(settings.display.maxNumbersPerActor == 512, "per actor upper clamps to active cap");
        Check(Near(settings.font.fontSize, 128.0F), "font size upper clamps");
        Check(settings.font.fontName == "Skyrim – UTF8", "utf8 string survives parsing");
        Check(Near(settings.animation.startScale, 0.1F), "start scale lower clamps");
        Check(Near(settings.animation.endScale, 5.0F), "end scale upper clamps");
        Check(Near(settings.animation.popDurationMs, 1.0F), "pop duration lower clamps");
        Check(Near(settings.colors.criticalDamageThreshold, settings.colors.largeDamageThreshold), "critical threshold normalizes above large");

        const auto highDistance = FDN::SettingsManager::LoadFromString(R"(
[Display]
fMaxDistance = 999999
)");
        Check(Near(highDistance.display.maxDistance, 100000.0F), "max distance upper clamps");
    }

    void TestDamageVisibilityPolicy()
    {
        FDN::Settings settings;
        Check(!settings.ShouldShowDamage(-1.0F), "negative damage hidden");
        Check(!settings.ShouldShowDamage(std::numeric_limits<float>::quiet_NaN()), "nan damage hidden");
        Check(!settings.ShouldShowDamage(std::numeric_limits<float>::infinity()), "infinite damage hidden");
        Check(!settings.ShouldShowDamage(FDN::kMaxDisplayableDamage + 1.0F), "absurd damage hidden");
        Check(!settings.ShouldShowDamage(0.0F), "zero damage hidden by default");
        settings.detection.showZeroDamage = true;
        Check(settings.ShouldShowDamage(0.0F), "zero damage can be enabled");
        settings.detection.showZeroDamage = false;
        settings.detection.minDamageToShow = 5.0F;
        Check(!settings.ShouldShowDamage(4.99F), "below threshold hidden");
        Check(settings.ShouldShowDamage(5.0F), "threshold damage shown");
        Check(settings.ShouldCaptureDamage(0.25F), "sub-threshold positive damage captured for aggregation");
        settings.detection.aggregateDamageOverTime = false;
        Check(!settings.ShouldCaptureDamage(4.99F), "sub-threshold damage hidden when aggregation disabled");
        Check(settings.ShouldCaptureDamage(5.0F), "threshold damage captured when aggregation disabled");
    }

    void TestColorParsingAndSelection()
    {
        const auto fallback = FDN::Color::FromARGB(0xFFFFFFFF);
        Check(FDN::ParseColor("#112233", fallback).ToARGB() == 0xFF112233, "rgb color defaults alpha");
        Check(FDN::ParseColor("0x80112233", fallback).ToARGB() == 0x80112233, "hex argb parses");
        Check(FDN::ParseColor("bad", fallback).ToARGB() == 0xFFFFFFFF, "bad color falls back");

        FDN::Settings settings;
        Check(settings.ColorFor(FDN::DamageType::Normal, 1.0F).ToARGB() == settings.colors.normalDamageColor.ToARGB(), "normal color selected");
        Check(settings.ColorFor(FDN::DamageType::Magic, 1.0F).ToARGB() == settings.colors.magicDamageColor.ToARGB(), "magic color selected");
        Check(settings.ColorFor(FDN::DamageType::Fire, 1.0F).ToARGB() == settings.colors.fireDamageColor.ToARGB(), "fire color selected");
        Check(settings.ColorFor(FDN::DamageType::Frost, 1.0F).ToARGB() == settings.colors.frostDamageColor.ToARGB(), "frost color selected");
        Check(settings.ColorFor(FDN::DamageType::Shock, 1.0F).ToARGB() == settings.colors.shockDamageColor.ToARGB(), "shock color selected");
        Check(settings.ColorFor(FDN::DamageType::Poison, 1.0F).ToARGB() == settings.colors.poisonDamageColor.ToARGB(), "poison color selected");
        Check(settings.ColorFor(FDN::DamageType::Normal, settings.colors.largeDamageThreshold).ToARGB() == settings.colors.largeDamageColor.ToARGB(), "large color selected");
        Check(settings.ColorFor(FDN::DamageType::Normal, settings.colors.criticalDamageThreshold).ToARGB() == settings.colors.criticalDamageColor.ToARGB(), "critical color selected");
        Check(settings.ColorFor(FDN::DamageType::PlayerTaken, 1.0F).ToARGB() == settings.colors.playerTakenDamageColor.ToARGB(), "player taken color selected");
    }

    void TestAggregation()
    {
        FDN::DamageAggregator aggregator(75.0F);
        const auto first = aggregator.Submit(Event(10, 1, 12.0F, 100.0));
        const auto merged = aggregator.Submit(Event(10, 1, 7.0F, 150.0));
        const auto separate = aggregator.Submit(Event(10, 1, 5.0F, 300.0));
        const auto differentTarget = aggregator.Submit(Event(11, 1, 5.0F, 310.0));
        const auto differentSource = aggregator.Submit(Event(10, 2, 5.0F, 320.0));
        const auto differentType = aggregator.Submit(Event(10, 1, 5.0F, 330.0, FDN::DamageType::Magic));

        Check(!first.merged && Near(first.amount, 12.0F), "first aggregate is fresh");
        Check(merged.merged && Near(merged.amount, 19.0F), "second aggregate merges");
        Check(first.id != 0 && merged.id == first.id, "merged aggregate keeps stable id");
        Check(!separate.merged && Near(separate.amount, 5.0F), "expired aggregate is fresh");
        Check(separate.id != first.id, "fresh aggregate receives new id");
        Check(!differentTarget.merged, "different target does not merge");
        Check(!differentSource.merged, "different source does not merge");
        Check(!differentType.merged, "different damage type does not merge");

        aggregator.ClearExpired(1000.0);
        Check(aggregator.ActiveCount() == 0, "expired aggregates clear");
    }

    void TestImpactPointAggregation()
    {
        FDN::DamageAggregator aggregator(75.0F);
        const FDN::WorldPoint firstPoint{ 10.0F, 20.0F, 30.0F };
        const FDN::WorldPoint secondPoint{ 40.0F, 50.0F, 60.0F };

        const auto first = aggregator.Submit(EventAt(10, 1, 12.0F, 100.0, firstPoint));
        Check(first.hasImpactPoint, "fresh aggregate stores impact point");
        Check(
            Near(first.impactPoint.x, firstPoint.x) &&
                Near(first.impactPoint.y, firstPoint.y) &&
                Near(first.impactPoint.z, firstPoint.z),
            "fresh impact point coordinates stored");

        const auto merged = aggregator.Submit(EventAt(10, 1, 7.0F, 150.0, secondPoint));
        Check(merged.merged, "impact aggregate merges");
        Check(merged.hasImpactPoint, "merged aggregate keeps impact point");
        Check(
            Near(merged.impactPoint.x, secondPoint.x) &&
                Near(merged.impactPoint.y, secondPoint.y) &&
                Near(merged.impactPoint.z, secondPoint.z),
            "merged aggregate uses latest impact point");

        const auto mergedWithoutPoint = aggregator.Submit(Event(10, 1, 1.0F, 160.0));
        Check(mergedWithoutPoint.hasImpactPoint, "merge without point preserves latest impact point");
        Check(Near(mergedWithoutPoint.impactPoint.x, secondPoint.x), "preserved impact point remains latest");
    }

    void TestAggregationBounds()
    {
        FDN::DamageAggregator aggregator(1000.0F);
        for (std::uint32_t i = 0; i < 10000; ++i) {
            [[maybe_unused]] const auto submitted = aggregator.Submit(Event(i + 1, 1, 1.0F, static_cast<double>(i)));
        }
        Check(aggregator.ActiveCount() <= 2048, "aggregator remains bounded under unique event stress");

        FDN::DamageAggregator sameActor(1000.0F);
        FDN::AggregatedDamage last;
        for (std::uint32_t i = 0; i < 1000; ++i) {
            last = sameActor.Submit(Event(1, 1, 1.0F, static_cast<double>(i)));
        }
        Check(sameActor.ActiveCount() == 1, "same actor/source stress keeps one aggregate");
        Check(last.merged && Near(last.amount, 1000.0F), "same actor/source stress accumulates damage");
    }

    void TestAnimation()
    {
        FDN::Settings settings;
        const auto frame0 = FDN::ComputeAnimationFrame(0.0F, 1.0F, 1.0F, settings.display, settings.animation);
        const auto frameMid = FDN::ComputeAnimationFrame(0.5F, 1.0F, 1.0F, settings.display, settings.animation);
        const auto frameEnd = FDN::ComputeAnimationFrame(1.2F, 1.0F, 1.0F, settings.display, settings.animation);

        Check(frame0.alive, "start frame alive");
        Check(frameMid.offsetY > frame0.offsetY, "number rises");
        Check(frameMid.alpha < frame0.alpha, "number fades");
        Check(!frameEnd.alive, "expired frame not alive");

        settings.animation.effectsEnabled = false;
        const auto disabled = FDN::ComputeAnimationFrame(0.5F, 1.0F, 1.0F, settings.display, settings.animation);
        Check(Near(disabled.alpha, 1.0F), "effects disabled keeps alpha");
        Check(Near(disabled.scale, 1.0F), "effects disabled keeps scale");

        settings.animation.effectsEnabled = true;
        settings.animation.popAnimation = false;
        const auto noPop = FDN::ComputeAnimationFrame(0.01F, 1.0F, 1.0F, settings.display, settings.animation);
        Check(Near(noPop.scale, settings.animation.endScale), "pop disabled uses end scale");
    }

    void TestPoolingLifetimeAndCaps()
    {
        FDN::Settings settings;
        settings.display.maxActiveNumbers = 2;
        settings.display.maxNumbersPerActor = 1;
        settings.display.lifetime = 0.25F;
        FDN::FloatingNumberManager manager(settings);
        FDN::DamageAggregator aggregator(settings.detection.mergeWindowMs);

        const auto first = manager.Submit(aggregator.Submit(Event(1, 14, 10.0F, 1.0)));
        const auto second = manager.Submit(aggregator.Submit(Event(1, 15, 11.0F, 2.0)));
        Check(first.has_value(), "first pooled number created");
        Check(second.has_value(), "per-actor limit recycles a slot");
        Check(manager.ActiveCount() == 1, "per-actor cap respected");
        Check(second->serial == first->serial, "forced recycle reuses visual serial");

        const auto& live = manager.Update(0.1F);
        Check(live.size() == 1 && live.front().animation.alive, "live update produced");
        const auto& expired = manager.Update(0.2F);
        Check(expired.size() == 1 && !expired.front().animation.alive, "expiration update produced");
        Check(manager.ActiveCount() == 0, "expired number inactive");

        settings.display.maxActiveNumbers = 2;
        settings.display.maxNumbersPerActor = 2;
        FDN::FloatingNumberManager capped(settings);
        [[maybe_unused]] const auto cappedFirst = capped.Submit(aggregator.Submit(Event(10, 1, 1.0F, 100.0)));
        [[maybe_unused]] const auto& cappedUpdate = capped.Update(0.2F);
        [[maybe_unused]] const auto cappedSecond = capped.Submit(aggregator.Submit(Event(20, 1, 1.0F, 200.0)));
        [[maybe_unused]] const auto cappedThird = capped.Submit(aggregator.Submit(Event(30, 1, 1.0F, 300.0)));
        Check(capped.ActiveCount() == 2, "global active cap respected");
    }

    void TestManagerMergesLatestAggregate()
    {
        FDN::Settings settings;
        settings.display.maxActiveNumbers = 4;
        settings.display.maxNumbersPerActor = 4;
        settings.display.lifetime = 10.0F;

        FDN::DamageAggregator aggregator(75.0F);
        FDN::FloatingNumberManager manager(settings);

        const auto first = manager.Submit(aggregator.Submit(Event(1, 14, 10.0F, 0.0)));
        const auto second = manager.Submit(aggregator.Submit(Event(1, 14, 20.0F, 200.0)));
        const auto mergedDamage = aggregator.Submit(Event(1, 14, 5.0F, 240.0));
        const auto merged = manager.Submit(mergedDamage);

        Check(first.has_value(), "first same-key visual created");
        Check(second.has_value(), "second same-key visual created after merge window");
        Check(mergedDamage.merged, "third same-key event merges into latest aggregate");
        Check(merged.has_value(), "latest aggregate merge updates a visual");
        Check(second && first && second->serial != first->serial, "separate same-key aggregates use separate visuals");
        Check(merged && second && merged->serial == second->serial, "merged same-key aggregate updates latest visual");
        Check(merged && merged->text == "25", "latest visual receives merged amount");
        Check(manager.ActiveCount() == 2, "old same-key visual remains independent");
    }

    void TestManagerUsesImpactPointOffset()
    {
        FDN::Settings settings;
        settings.display.startVerticalOffset = 120.0F;
        settings.display.impactPointVerticalOffset = 18.0F;

        FDN::DamageAggregator aggregator(settings.detection.mergeWindowMs);
        FDN::FloatingNumberManager manager(settings);

        const auto actorAnchored = manager.Submit(aggregator.Submit(Event(1, 14, 10.0F, 1.0)));
        Check(actorAnchored.has_value(), "actor-anchored update created");
        Check(actorAnchored && !actorAnchored->hasImpactPoint, "actor-anchored update has no impact point");
        Check(
            actorAnchored && Near(actorAnchored->animation.offsetY, settings.display.startVerticalOffset),
            "actor-anchored update uses actor offset");

        const FDN::WorldPoint impactPoint{ 1.0F, 2.0F, 3.0F };
        const auto impactAnchored = manager.Submit(aggregator.Submit(EventAt(2, 14, 10.0F, 2.0, impactPoint)));
        Check(impactAnchored.has_value(), "impact-anchored update created");
        Check(impactAnchored && impactAnchored->hasImpactPoint, "impact-anchored update carries point");
        Check(impactAnchored && Near(impactAnchored->impactPoint.z, impactPoint.z), "impact-anchored update carries coordinates");
        Check(
            impactAnchored && Near(impactAnchored->animation.offsetY, settings.display.impactPointVerticalOffset),
            "impact-anchored update uses impact offset");
    }

    void TestFiltering()
    {
        FDN::Settings settings;
        Check(FDN::ShouldDisplayContext(Context(FDN::SourceRelation::Player, FDN::TargetRelation::HostileToPlayer), settings), "default player hostile damage allowed");

        Check(!FDN::ShouldDisplayContext(Context(FDN::SourceRelation::Follower, FDN::TargetRelation::HostileToPlayer), settings), "followers off by default");
        settings.general.showFollowerDealtDamage = true;
        Check(FDN::ShouldDisplayContext(Context(FDN::SourceRelation::Follower, FDN::TargetRelation::HostileToPlayer), settings), "followers can be enabled");

        Check(!FDN::ShouldDisplayContext(Context(FDN::SourceRelation::NPC, FDN::TargetRelation::HostileToPlayer), settings), "npc vs npc off by default");
        settings.general.showNPCvsNPCDamage = true;
        Check(FDN::ShouldDisplayContext(Context(FDN::SourceRelation::NPC, FDN::TargetRelation::HostileToPlayer), settings), "npc vs npc can be enabled");

        Check(!FDN::ShouldDisplayContext(Context(FDN::SourceRelation::NPC, FDN::TargetRelation::Player, true), settings), "player taken off by default");
        settings.general.showDamageTakenByPlayer = true;
        Check(FDN::ShouldDisplayContext(Context(FDN::SourceRelation::NPC, FDN::TargetRelation::Player, true), settings), "player taken can be enabled");

        settings.general.showPlayerDealtDamage = true;
        Check(!FDN::ShouldDisplayContext(Context(FDN::SourceRelation::Player, FDN::TargetRelation::NonHostile), settings), "non-hostile off by default");
        settings.general.showNonHostileTargets = true;
        Check(FDN::ShouldDisplayContext(Context(FDN::SourceRelation::Player, FDN::TargetRelation::NonHostile), settings), "non-hostile can be enabled");

        auto invalid = Context(FDN::SourceRelation::Player, FDN::TargetRelation::HostileToPlayer);
        invalid.target3DLoaded = false;
        Check(!FDN::ShouldDisplayContext(invalid, settings), "unloaded actor hidden");
        invalid = Context(FDN::SourceRelation::Player, FDN::TargetRelation::HostileToPlayer);
        invalid.targetIsDisabled = true;
        Check(!FDN::ShouldDisplayContext(invalid, settings), "disabled actor hidden");
        invalid = Context(FDN::SourceRelation::Player, FDN::TargetRelation::HostileToPlayer);
        invalid.targetIsDeleted = true;
        Check(!FDN::ShouldDisplayContext(invalid, settings), "deleted actor hidden");
    }

    void TestFormatting()
    {
        FDN::Settings settings;
        Check(settings.FormatDamage(12.4F) == "12", "integer rounding down");
        Check(settings.FormatDamage(12.5F) == "13", "integer rounding up");
        settings.detection.roundDamage = false;
        settings.detection.decimalPlaces = 3;
        Check(settings.FormatDamage(12.3456F) == "12.346", "decimal formatting");
        settings.detection.decimalPlaces = 99;
        Check(settings.FormatDamage(1.23456F) == "1.2346", "format clamps decimal places at use site");
    }

    void TestStressBoundedManager()
    {
        FDN::Settings settings;
        settings.display.maxActiveNumbers = 128;
        settings.display.maxNumbersPerActor = 8;
        FDN::FloatingNumberManager manager(settings);
        FDN::DamageAggregator aggregator(settings.detection.mergeWindowMs);

        for (std::uint32_t i = 0; i < 10000; ++i) {
            const auto target = static_cast<FDN::ActorKey>((i % 256) + 1);
            const auto source = static_cast<FDN::ActorKey>((i % 8) + 1);
            [[maybe_unused]] const auto submitted = manager.Submit(aggregator.Submit(Event(target, source, 1.0F, static_cast<double>(i))));
            if (i % 64 == 0) {
                [[maybe_unused]] const auto& frame = manager.Update(1.0F / 60.0F);
            }
        }

        Check(manager.ActiveCount() <= 128, "manager active count bounded under 10000 events");
        Check(aggregator.ActiveCount() <= 2048, "aggregator active count bounded under 10000 events");

        const auto& updates = manager.Update(100.0F);
        Check(updates.size() <= 128, "expiration update bounded by pool size");
        Check(manager.ActiveCount() == 0, "stress numbers expire");
    }
}

int main()
{
    TestIniParsing();
    TestMalformedIniAndClamping();
    TestDamageVisibilityPolicy();
    TestColorParsingAndSelection();
    TestAggregation();
    TestImpactPointAggregation();
    TestAggregationBounds();
    TestAnimation();
    TestPoolingLifetimeAndCaps();
    TestManagerMergesLatestAggregate();
    TestManagerUsesImpactPointOffset();
    TestFiltering();
    TestFormatting();
    TestStressBoundedManager();

    if (failures != 0) {
        std::cerr << failures << " test failure(s)\n";
        return EXIT_FAILURE;
    }

    std::cout << "All FDN core tests passed\n";
    return EXIT_SUCCESS;
}
