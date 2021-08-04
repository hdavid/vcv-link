/*

 VCV-Link by Stellare Modular
 Copyright (C) 2017-2020 - Vincenzo Pietropaolo, Sander Baan

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/

#include "Link.hpp"
#include "LinkPeer.hpp"
#include "UIWidgets.hpp"

struct Link : Module
{
public:
	enum ParamIds
    {
        SYNC_PARAM = 0,
        OFFSET_PARAM,
        SWING_PARAM,
        NUM_PARAMS
    };

	enum InputIds
    {
		BPM_INPUT = 0,
        NUM_INPUTS
	};

	enum OutputIds
    {
        CLOCK_OUTPUT_4TH = 0,
		RESET_OUTPUT,
        CLOCK_OUTPUT_2ND,
		NUM_OUTPUTS
	};

	enum LightIds
    {
        CLOCK_LIGHT_4TH = 0,
        RESET_LIGHT,
        SYNC_LIGHT,
        CLOCK_LIGHT_2ND,
        NUM_LIGHTS
	};

    Link()
    {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);

        configParam(SYNC_PARAM, 0.0, 1.0, 0.0);
        configParam(SWING_PARAM, 0.0, 1.0, 0.0);
        configParam(OFFSET_PARAM, -1.0, 1.0, 0.0);

        LinkPeer::attachModule();
    }

    ~Link()
    {
        LinkPeer::detachModule();
    }

    void process(const ProcessArgs& args) override;

private:
    void clampTick(int& tick, int maxTicks);
	
	float m_bpm = 120;
    int m_lastTick = -1;
    bool m_synced = false;
};

void Link::clampTick(int& tick, int maxTicks)
{
    if (tick < 0)
        tick += maxTicks;

    tick %= maxTicks;
}

void Link::process(const ProcessArgs& args)
{
    // Tick length = 1 beat / 16ths / 2 (PWM 50%)
    static const double tick_length = (1.0 / 16.0) / 2.0;
    // We assume 4/4 signature
    static const double beats_per_bar = 4.0;
    static const int ticks_per_bar = static_cast<int>(beats_per_bar / tick_length);

	if (params[SYNC_PARAM].getValue() == 1.0)
	{
		m_synced = false;
	}

    double phase = 0.0;

    if (api0::ablLink)
    {
        phase = ABLLinkPhaseAtTime(api0::INSTANCE->ablState, api0::engineGetCurrentStepHostTime(), beats_per_bar);
        if (inputs[BPM_INPUT].active) {
            float bpm_in = inputs[BPM_INPUT].value;
            if (bpm_in < 0) {
                //use negative values for linear mapping, using 100bpm.
                bpm_in = -1 * bpm_in * 100;
            } else {
                // and positive for logarithmic, using 120
                bpm_in = powf(bpm_in, 2.0f) * 120;
            }
            //fix incorrect bpm values
            if (bpm_in>999){
                bpm_in = 999;
            }
            if (bpm_in<20){
                bpm_in = 20;
            }
            
            if (bpm_in != m_bpm) {
                m_bpm = bpm_in;
                auto timeline = ABLLinkCaptureAudioSessionState(api0::ablLink);
				ABLLinkSetTempo(timeline, m_bpm, api0::engineGetCurrentStepHostTime());
				ABLLinkCommitAudioSessionState(api0::ablLink, timeline);
            }

		}
    }

    const double offset = params[OFFSET_PARAM].getValue() * (5.0 * tick_length);
    int tick = static_cast<int>(std::floor((phase + offset) / tick_length));

    clampTick(tick, ticks_per_bar);

    if (((tick >> 3) % 2) == 1)
    {
        const double max_swing_in_ticks = 3.0;

        const double swing = params[SWING_PARAM].getValue() * (max_swing_in_ticks * tick_length);
        tick = static_cast<int>(std::floor((phase + offset - swing) / tick_length));

        clampTick(tick, ticks_per_bar);
    }

    tick %= ticks_per_bar;

    if ((m_lastTick != tick) || !m_synced)
    {
        if (tick == 0)
            m_synced = true;

        if (m_synced)
        {
            // 8 ticks per 4th of beat, clock has 50% PWM
            const bool clock_4th = ((tick % 8) < 4);
            outputs[CLOCK_OUTPUT_4TH].setVoltage(clock_4th ? 10.0 : 0.0);
            lights[CLOCK_LIGHT_4TH].setBrightness(clock_4th ? 1.0 : 0.0);

            const bool clock_2nd = ((tick % 16) < 8);
            outputs[CLOCK_OUTPUT_2ND].setVoltage(clock_2nd ? 10.0 : 0.0);
            lights[CLOCK_LIGHT_2ND].setBrightness(clock_2nd ? 1.0 : 0.0);

            // reset has 25% PWM
            const bool reset = ((tick % ticks_per_bar) < 2);
            outputs[RESET_OUTPUT].setVoltage(reset ? 10.0 : 0.0);
            lights[RESET_LIGHT].setBrightness(reset ? 1.0 : 0.0);
        }
        else
        {
            outputs[CLOCK_OUTPUT_4TH].setVoltage(0.0);
            lights[CLOCK_LIGHT_4TH].setBrightness(0.0);

            outputs[CLOCK_OUTPUT_2ND].setVoltage(0.0);
            lights[CLOCK_LIGHT_2ND].setBrightness(0.0);

            outputs[RESET_OUTPUT].setVoltage(0.0);
            lights[RESET_LIGHT].setBrightness(0.0);
        }

        m_lastTick = tick;
    }

    lights[SYNC_LIGHT].setBrightness(m_synced ? 0.0 : 1.0);
}

struct LinkWidget : ModuleWidget
{
    LinkWidget(Link*);
};

LinkWidget::LinkWidget(Link* module)
{
    setModule(module);
    box.size = Vec(60, 380);

    SvgPanel* panel = new SvgPanel();
    panel->box.size = box.size;
    panel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, "res/Link.svg")));
    addChild(panel);

    addChild(createWidget<StellareScrew>(Vec(0, 0)));
    addChild(createWidget<StellareScrew>(Vec(box.size.x - 15, 365)));

    addInput(createInput<StellareJack>(Vec(19, 30), module, Link::BPM_INPUT));
    
    addParam(createParam<StellarePushButton>(Vec(19.7, 155), module, Link::SYNC_PARAM));
    addParam(createParam<StellareKnob01>(Vec(16.2, 58), module, Link::OFFSET_PARAM));
    addParam(createParam<StellareKnob01>(Vec(16.2, 105.7), module, Link::SWING_PARAM));

    addOutput(createOutput<StellareJack>(Vec(11, 304), module, Link::CLOCK_OUTPUT_4TH));
    addOutput(createOutput<StellareJack>(Vec(27, 268), module, Link::CLOCK_OUTPUT_2ND));
    addOutput(createOutput<StellareJack>(Vec(26, 340), module, Link::RESET_OUTPUT));

    addChild(createLight<SmallLight<BlueLight>>(Vec(27.5, 297.5), module, Link::CLOCK_LIGHT_4TH));
    addChild(createLight<SmallLight<GreenLight>>(Vec(27.5, 261.5), module, Link::CLOCK_LIGHT_2ND));
    addChild(createLight<SmallLight<YellowLight>>(Vec(27.5, 333.4), module, Link::RESET_LIGHT));
    addChild(createLight<SmallLight<BlueLight>>(Vec(27, 162.3), module, Link::SYNC_LIGHT));
}

Model *modelLink = createModel<Link, LinkWidget>("Link");
