/*
 * ASoC-driver for digital amplifier Analog Devices SSM3582A
 *
 * Copyright (C) 2026, Nik Kov <nikkov@gmail.com>
 *
 * This driver supports I2S/TDM modes, power management (DAPM),
 * volume control, and Device Tree integration.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/bitops.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/tlv.h>

/* --- Register mapping SSM3582A --- */
#define SSM3582A_REG_VENDOR_ID          0x00
#define SSM3582A_REG_DEVICE_ID1         0x01
#define SSM3582A_REG_DEVICE_ID2         0x02
#define SSM3582A_REG_REVISION           0x03
#define SSM3582A_REG_POWER_CTRL         0x04
#define SSM3582A_REG_AMP_DAC_CTRL       0x05
#define SSM3582A_REG_DAC_CTRL           0x06
#define SSM3582A_REG_VOL_LEFT_CTRL      0x07
#define SSM3582A_REG_VOL_RIGHT_CTRL     0x08
#define SSM3582A_REG_SAI_CTRL1          0x09
#define SSM3582A_REG_SAI_CTRL2          0x0A
#define SSM3582A_REG_SLOT_LEFT_CTRL     0x0B
#define SSM3582A_REG_SLOT_RIGHT_CTRL    0x0C
#define SSM3582A_REG_LIM_LEFT_CTRL1     0x0E
#define SSM3582A_REG_LIM_LEFT_CTRL2     0x0F
#define SSM3582A_REG_LIM_LEFT_CTRL3     0x10
#define SSM3582A_REG_LIM_RIGHT_CTRL1    0x11
#define SSM3582A_REG_LIM_RIGHT_CTRL2    0x12
#define SSM3582A_REG_LIM_RIGHT_CTRL3    0x13
#define SSM3582A_REG_CLIP_LEFT_CTRL     0x14
#define SSM3582A_REG_CLIP_RIGHT_CTRL    0x15
#define SSM3582A_REG_FAULT_CTRL1        0x16
#define SSM3582A_REG_FAULT_CTRL2        0x17
#define SSM3582A_REG_STATUS1            0x18
#define SSM3582A_REG_STATUS2            0x19
#define SSM3582A_REG_VBAT               0x1A
#define SSM3582A_REG_TEMP               0x1B
#define SSM3582A_REG_SOFT_RESET         0x1C
#define SSM3582A_MAX_REGISTER           0x1C

/* --- Definition of the volume scale (TLV) --- */
// from -71.25 dB to +24.00 dB (step 0.375 dB calculated by core)
static const DECLARE_TLV_DB_MINMAX(ssm3582a_vol_tlv, -7125, 2400);

/* --- User controls (Kcontrols) --- */
static const struct snd_kcontrol_new ssm3582a_kcontrols[] = {
    // Volume control
    // inversion = 1, because 0x00 = +24 dB, and 0xFE = -71.25 dB
    SOC_DOUBLE_R_TLV("Playback Volume",
                     SSM3582A_REG_VOL_LEFT_CTRL,
                     SSM3582A_REG_VOL_RIGHT_CTRL,
                     0, 254, 1, ssm3582a_vol_tlv),
    // Mute switch
    // Register 0x06, bits 5 (left) and 6 (right). 1 = Mute, inverted = 1
    SOC_DOUBLE("Playback Switch",
               SSM3582A_REG_DAC_CTRL,
               5, 6, 1, 1),
};

/* --- DAPM (Dynamic Audio Power Management) --- */
static const struct snd_soc_dapm_widget ssm3582a_dapm_widgets[] = {
    SND_SOC_DAPM_AIF_IN("SAI", "Playback", 0, SND_SOC_NOPM, 0, 0),
    SND_SOC_DAPM_DAC("DAC", NULL, SND_SOC_NOPM, 0, 0),

    // Power Amplifier Control (Reg 0x04: bit 2 = Left, bit 3 = Right)
    SND_SOC_DAPM_PGA("Amp Left", SSM3582A_REG_POWER_CTRL, 2, 1, NULL, 0),
    SND_SOC_DAPM_PGA("Amp Right", SSM3582A_REG_POWER_CTRL, 3, 1, NULL, 0),

    SND_SOC_DAPM_OUTPUT("OUTL"),
    SND_SOC_DAPM_OUTPUT("OUTR"),
};

static const struct snd_soc_dapm_route ssm3582a_dapm_routes[] = {
    { "DAC", NULL, "SAI" },
    { "Amp Left", NULL, "DAC" },
    { "Amp Right", NULL, "DAC" },
    { "OUTL", NULL, "Amp Left" },
    { "OUTR", NULL, "Amp Right" },
};

/* --- DAI operations (Digital Audio Interface) --- */

// Setting up the bus format (I2S/TDM)
static int ssm3582a_dai_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
    struct snd_soc_component *component = dai->component;
    unsigned int sai_ctrl1 = 0;
    int ret = 0;

    // 1. Setting up a role (SSM3582A only supports Slave)
    switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
    case SND_SOC_DAIFMT_BC_FC:
        break;
    default:
        dev_err(component->dev, "Only Slave (Consumer) mode is supported\n");
        return -EINVAL;
    }

    // 2. Data Format (I2S, Left J, DSP_A/B)
    switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
    case SND_SOC_DAIFMT_I2S:
        sai_ctrl1 |= 0x00;
        break;
    case SND_SOC_DAIFMT_LEFT_J:
        sai_ctrl1 |= 0x02;
        break;
    case SND_SOC_DAIFMT_DSP_A:
        sai_ctrl1 |= 0x05;
        break;
    case SND_SOC_DAIFMT_DSP_B:
        sai_ctrl1 |= 0x01;
        break;
    default:
        return -EINVAL;
    }

    // 3. Inversion of the clocks
    switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
    case SND_SOC_DAIFMT_NB_NF:
        break;
    case SND_SOC_DAIFMT_IB_NF:
        sai_ctrl1 |= (1 << 6); 
        break;
    case SND_SOC_DAIFMT_NB_IF:
        sai_ctrl1 |= (1 << 2);
        break;
    case SND_SOC_DAIFMT_IB_IF:
        sai_ctrl1 |= (1 << 6) | (1 << 2);
        break;
    default:
        return -EINVAL;
    }

    ret = snd_soc_component_update_bits(component, SSM3582A_REG_SAI_CTRL1,
                                  0x47, sai_ctrl1);
    if(ret < 0) {
        dev_err(component->dev, "Failed to write value to ssm3582a SAI_CTRL1 register\n");
        return ret;
    }
    //dev_info(component->dev, "write value 0x%x to register 0x%x\n", sai_ctrl1, SSM3582A_REG_SAI_CTRL1);

    return 0;
}

// Setting the sampling frequency, bit depth, and slot width
static int ssm3582a_dai_hw_params(struct snd_pcm_substream *substream,
                                  struct snd_pcm_hw_params *params,
                                  struct snd_soc_dai *dai)
{
    struct snd_soc_component *component = dai->component;
    unsigned int rate = params_rate(params);
    unsigned int width = params_width(params);
    unsigned int slot_width = params_physical_width(params);
    unsigned int dac_fs = 0;
    unsigned int data_width_reg = 0;
    unsigned int tdm_bclks = 0;
    int ret = 0;

    // sampling frequency
    if (rate >= 8000 && rate <= 12000) dac_fs = 0x00;
    else if (rate >= 16000 && rate <= 24000) dac_fs = 0x01;
    else if (rate >= 32000 && rate <= 48000) dac_fs = 0x02;
    else if (rate >= 64000 && rate <= 96000) dac_fs = 0x03;
    else if (rate >= 128000 && rate <= 192000) dac_fs = 0x04;
    else return -EINVAL;

    ret = snd_soc_component_update_bits(component, SSM3582A_REG_DAC_CTRL,
                                  0x07, dac_fs);
    if(ret < 0) {
        dev_err(component->dev, "Failed to write value to ssm3582a DAC_CTRL register\n");
        return ret;
    }
    //dev_info(component->dev, "write value 0x%x to register 0x%x\n", dac_fs, SSM3582A_REG_DAC_CTRL);

    // Data bit depth (16 or 24/32 bits)
    if (width == 16) data_width_reg = 0x10;
    else if (width == 24 || width == 32) data_width_reg = 0x00;
    else return -EINVAL;

    ret = snd_soc_component_update_bits(component, SSM3582A_REG_SAI_CTRL2,
                                  0x10, data_width_reg);
    if(ret < 0) {
        dev_err(component->dev, "Failed to write value to ssm3582a SAI_CTRL2 register\n");
        return ret;
    }
    //dev_info(component->dev, "write value 0x%x to register 0x%x\n", data_width_reg, SSM3582A_REG_SAI_CTRL2);

    // Slot width TDM/I2S
    switch (slot_width) {
    case 16: tdm_bclks = 0x00; break;
    case 24: tdm_bclks = 0x08; break;
    case 32: tdm_bclks = 0x10; break;
    case 48: tdm_bclks = 0x18; break;
    case 64: tdm_bclks = 0x20; break;
    default: return -EINVAL;
    }

    ret = snd_soc_component_update_bits(component, SSM3582A_REG_SAI_CTRL1,
                                  0x38, tdm_bclks);
    if(ret < 0) {
        dev_err(component->dev, "Failed to write value to ssm3582a SAI_CTRL1 register\n");
        return ret;
    }
    //dev_info(component->dev, "write value 0x%x to register 0x%x\n", tdm_bclks, SSM3582A_REG_SAI_CTRL1);
    return 0;
}

// Setting up TDM slots
static int ssm3582a_set_tdm_slot(struct snd_soc_dai *dai, unsigned int tx_mask,
                                 unsigned int rx_mask, int slots, int slot_width)
{
    struct snd_soc_component *component = dai->component;
    int left_slot = ffs(rx_mask) - 1;
    int right_slot = fls(rx_mask) - 1;
    int ret = 0;

    if (left_slot < 0 || right_slot < 0)
        return -EINVAL;

    ret = snd_soc_component_update_bits(component, SSM3582A_REG_SLOT_LEFT_CTRL,
                                  0x1F, left_slot);
    if(ret < 0) {
        dev_err(component->dev, "Failed to write value to ssm3582a SLOT_LEFT_CTRL register\n");
        return ret;
    }
    //dev_info(component->dev, "write value 0x%x to register 0x%x\n", left_slot, SSM3582A_REG_SLOT_LEFT_CTRL);

    ret = snd_soc_component_update_bits(component, SSM3582A_REG_SLOT_RIGHT_CTRL,
                                  0x1F, right_slot);
    if(ret < 0) {
        dev_err(component->dev, "Failed to write value to ssm3582a SLOT_RIGHT_CTRL register\n");
        return ret;
    }
    //dev_info(component->dev, "write value 0x%x to register 0x%x\n", right_slot, SSM3582A_REG_SLOT_RIGHT_CTRL);
    return 0;
}

static const struct snd_soc_dai_ops ssm3582a_dai_ops = {
    .set_fmt      = ssm3582a_dai_set_fmt,
    .hw_params    = ssm3582a_dai_hw_params,
    .set_tdm_slot = ssm3582a_set_tdm_slot,
};

static struct snd_soc_dai_driver ssm3582a_dai = {
    .name = "ssm3582a-dai",
    .playback = {
        .stream_name  = "Playback",
        .channels_min = 1,
        .channels_max = 8,
        .rates        = SNDRV_PCM_RATE_8000_192000,
        .formats      = /*SNDRV_PCM_FMTBIT_S16_LE |*/ SNDRV_PCM_FMTBIT_S24_LE |
                        SNDRV_PCM_FMTBIT_S32_LE,
    },
    .ops = &ssm3582a_dai_ops,
};

static struct snd_soc_component_driver soc_component_dev_ssm3582a = {
    .controls       = ssm3582a_kcontrols,
    .num_controls   = ARRAY_SIZE(ssm3582a_kcontrols),
    .dapm_widgets   = ssm3582a_dapm_widgets,
    .num_dapm_widgets = ARRAY_SIZE(ssm3582a_dapm_widgets),
    .dapm_routes    = ssm3582a_dapm_routes,
    .num_dapm_routes= ARRAY_SIZE(ssm3582a_dapm_routes),
};

/* --- Regmap configuration --- */
static bool ssm3582a_volatile_register(struct device *dev, unsigned int reg)
{
    switch (reg) {
    case SSM3582A_REG_VENDOR_ID:
    case SSM3582A_REG_DEVICE_ID1:
    case SSM3582A_REG_DEVICE_ID2:
    case SSM3582A_REG_REVISION:
    case SSM3582A_REG_STATUS1:
    case SSM3582A_REG_STATUS2:
    case SSM3582A_REG_VBAT:
    case SSM3582A_REG_TEMP:
        return true; // Don't cache statuses and sensors
    default:
        return false;
    }
}

static const struct regmap_config ssm3582a_regmap_config = {
    .reg_bits = 8,
    .val_bits = 8,
    .max_register = SSM3582A_MAX_REGISTER,
    .cache_type = REGCACHE_RBTREE,
    .volatile_reg = ssm3582a_volatile_register,
};

/* --- Initialization (Probe) --- */
static int ssm3582a_i2c_probe(struct i2c_client *client)
{
    struct device *dev = &client->dev;
    struct regmap *regmap;
    unsigned int reg_val;
    int ret, count;
    u8 *init_data;

    // 1. Initialization regmap
    regmap = devm_regmap_init_i2c(client, &ssm3582a_regmap_config);
    if (IS_ERR(regmap)) {
        dev_err(dev, "Failed to initialize regmap\n");
        return PTR_ERR(regmap);
    }

    // 2. Reading the chip ID and revision (connection check)
    ret = regmap_read(regmap, SSM3582A_REG_REVISION, &reg_val);
    if (ret < 0) {
        dev_err(dev, "Communication error with I2C device\n");
        return ret;
    }
    dev_info(dev, "SSM3582A detected (Revision: 0x%x)\n", reg_val);

    // 3. Software reset to default state
    regmap_write(regmap, SSM3582A_REG_SOFT_RESET, 0x01);
    usleep_range(2000, 3000); // Timeout for the chip core to reboot

    count = device_property_count_u8(dev, "init-registers");
    if (count > 0) {
        // need pair address-value
        if (count % 2 != 0) {
            dev_err(dev, "init-registers property must have even number of elements\n");
            return -EINVAL;
        }
        init_data = devm_kcalloc(dev, count, sizeof(u8), GFP_KERNEL);
        if (!init_data)
            return -ENOMEM;
        ret = device_property_read_u8_array(dev, "init-registers", init_data, count);
        if (ret) {
            dev_err(dev, "Failed to read init-registers: %d\n", ret);
            return ret;
        }

        // write init values to device
        for (int i = 0; i < count; i += 2) {
            u8 reg = init_data[i];
            u8 val = init_data[i + 1];

            ret = regmap_write(regmap, reg, val);
            if (ret) {
                dev_err(dev, "Failed to write reg 0x%x: %d\n", reg, ret);
                return ret;
            }
            dev_info(dev, "Init: wrote 0x%02x to reg 0x%02x\n", val, reg);
        }
        kfree(init_data);
    }

    // 4. Registering a Component with ASoC
    ret = devm_snd_soc_register_component(dev, &soc_component_dev_ssm3582a,
                                          &ssm3582a_dai, 1);
    if (ret < 0) {
        dev_err(dev, "Failed to register ASoC component\n");
        return ret;
    }

    return 0;
}

/* --- Description and registration of the module --- */
static const struct i2c_device_id ssm3582a_i2c_id[] = {
    { "ssm3582a", 0 },
    { }
};
MODULE_DEVICE_TABLE(i2c, ssm3582a_i2c_id);

// Compatibility table for Device Tree (.dts files)
static const struct of_device_id ssm3582a_of_match[] = {
    { .compatible = "adi,ssm3582a", },
    { }
};
MODULE_DEVICE_TABLE(of, ssm3582a_of_match);

static struct i2c_driver ssm3582a_i2c_driver = {
    .driver = {
        .name = "ssm3582a",
        .owner = THIS_MODULE,
        .of_match_table = ssm3582a_of_match,
    },
    .probe = ssm3582a_i2c_probe,
    .id_table = ssm3582a_i2c_id,
};

module_i2c_driver(ssm3582a_i2c_driver);

MODULE_AUTHOR("Nik Kov <nikkov@gmail.com>");
MODULE_DESCRIPTION("ASoC Driver for Analog Devices SSM3582A Audio Amplifier");
MODULE_LICENSE("GPL v2");
