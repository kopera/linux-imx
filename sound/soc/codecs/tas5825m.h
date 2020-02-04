#ifndef __TAS5825M_H__
#define __TAS5825M_H__

#define TAS5825M_REG(book_id, page_id, reg_id)              (\
                                                                (((book_id) & 0xff) << 16) |\
                                                                (((page_id) & 0xff) << 8) |\
                                                                ((reg_id) & 0x7f)\
                                                            )
/*  We use 0xff instead of 0x7f in TAS5825M_REG_REG_ID to detect invalid
    registers. Invalid registers could be created though the regmap debugfs
    interface.
*/
#define TAS5825M_REG_REG_ID(reg)                            ((reg) & 0xff)
#define TAS5825M_REG_PAGE_ID(reg)                           (((reg) >> 8) & 0xff)
#define TAS5825M_REG_BOOK_ID(reg)                           (((reg) >> 16) & 0xff)

#define TAS5825M_REG_IS_PAGE_SELECT(reg)                    (TAS5825M_REG_REG_ID(reg) == 0x00)
#define TAS5825M_REG_IS_BOOK_SELECT(reg)                    ((TAS5825M_REG_REG_ID(reg) == 0x7f) && (TAS5825M_REG_PAGE_ID(reg) == 0x00))

#define TAS5825M_REG_DEVICE_CTRL2                           TAS5825M_REG(0x00, 0x00, 0x03)
#define TAS5825M_REG_DEVICE_CTRL2_MUTE_MASK                 (1 << 3)
#define TAS5825M_REG_DEVICE_CTRL2_MUTED                     (1 << 3)
#define TAS5825M_REG_DEVICE_CTRL2_UNMUTED                   (0 << 3)

#define TAS5825M_REG_SIG_CH_CTRL                            TAS5825M_REG(0x00, 0x00, 0x28)
#define TAS5825M_REG_SIG_CH_CTRL_FSMODE_MASK                (15)
#define TAS5825M_REG_SIG_CH_CTRL_FSMODE_AUTO                (0)
#define TAS5825M_REG_SIG_CH_CTRL_FSMODE_32KHZ               (6)
#define TAS5825M_REG_SIG_CH_CTRL_FSMODE_44_1KHZ             (8)
#define TAS5825M_REG_SIG_CH_CTRL_FSMODE_48KHZ               (9)
#define TAS5825M_REG_SIG_CH_CTRL_FSMODE_88_2KHZ             (10)
#define TAS5825M_REG_SIG_CH_CTRL_FSMODE_96KHZ               (11)
#define TAS5825M_REG_SIG_CH_CTRL_FSMODE_176_4KHZ            (12)
#define TAS5825M_REG_SIG_CH_CTRL_FSMODE_192KHZ              (13)

#define TAS5825M_REG_I2S_CTRL                               TAS5825M_REG(0x00, 0x00, 0x31)
#define TAS5825M_REG_I2S_CTRL_SCLK_INV_MASK                 (1 << 5)
#define TAS5825M_REG_I2S_CTRL_SCLK_INV_NORMAL               (0 << 5)
#define TAS5825M_REG_I2S_CTRL_SCLK_INV_INVERTED             (1 << 5)

#define TAS5825M_REG_SAP_CTRL1                              TAS5825M_REG(0x00, 0x00, 0x33)
#define TAS5825M_REG_SAP_CTRL1_I2S_SHIFT_MSB_MASK           (1 << 7)
#define TAS5825M_REG_SAP_CTRL1_I2S_SHIFT_MSB_SHIFTED        (1 << 7)
#define TAS5825M_REG_SAP_CTRL1_I2S_SHIFT_MSB_NORMAL         (0 << 7)
#define TAS5825M_REG_SAP_CTRL1_DATA_FORMAT_MASK             (3 << 4)
#define TAS5825M_REG_SAP_CTRL1_DATA_FORMAT_I2S              (0 << 4)
#define TAS5825M_REG_SAP_CTRL1_DATA_FORMAT_TDM              (1 << 4)
#define TAS5825M_REG_SAP_CTRL1_DATA_FORMAT_DSP              (1 << 4)
#define TAS5825M_REG_SAP_CTRL1_DATA_FORMAT_RTJ              (2 << 4)
#define TAS5825M_REG_SAP_CTRL1_DATA_FORMAT_LTJ              (3 << 4)
#define TAS5825M_REG_SAP_CTRL1_WORD_LENGTH_MASK             (3 << 0)
#define TAS5825M_REG_SAP_CTRL1_WORD_LENGTH_16               (0 << 0)
#define TAS5825M_REG_SAP_CTRL1_WORD_LENGTH_20               (1 << 0)
#define TAS5825M_REG_SAP_CTRL1_WORD_LENGTH_24               (2 << 0)
#define TAS5825M_REG_SAP_CTRL1_WORD_LENGTH_32               (3 << 0)

#define TAS5825M_REG_SAP_CTRL3                              TAS5825M_REG(0x00, 0x00, 0x35)
#define TAS5825M_REG_SAP_CTRL3_LEFT_DAC_DPATH_MASK          (3 << 4)
#define TAS5825M_REG_SAP_CTRL3_LEFT_DAC_DPATH_ZERO          (0 << 4)
#define TAS5825M_REG_SAP_CTRL3_LEFT_DAC_DPATH_LEFT          (1 << 4)
#define TAS5825M_REG_SAP_CTRL3_LEFT_DAC_DPATH_RIGHT         (2 << 4)
#define TAS5825M_REG_SAP_CTRL3_RIGHT_DAC_DPATH_MASK         (3 << 0)
#define TAS5825M_REG_SAP_CTRL3_RIGHT_DAC_DPATH_ZERO         (0 << 0)
#define TAS5825M_REG_SAP_CTRL3_RIGHT_DAC_DPATH_RIGHT        (1 << 0)
#define TAS5825M_REG_SAP_CTRL3_RIGHT_DAC_DPATH_LEFT         (2 << 0)

#define TAS5825M_REG_DIG_VOL                                TAS5825M_REG(0x00, 0x00, 0x4c)

#define TAS5825M_REG_AGAIN                                  TAS5825M_REG(0x00, 0x00, 0x54)

#define TAS5825M_REG_DSP_VOL_LEFT                           TAS5825M_REG(0x8c, 0x0b, 0x0c)
#define TAS5825M_REG_DSP_VOL_RIGHT                          TAS5825M_REG(0x8c, 0x0b, 0x10)

#define TAS5825M_REG_DSP_EQ_GANG                            TAS5825M_REG(0x8c, 0x0b, 0x28)
#define TAS5825M_REG_DSP_EQ_BYPASS                          TAS5825M_REG(0x8c, 0x0b, 0x2c)

#define TAS5825M_REG_DSP_EQ_BQ_1_LEFT                       TAS5825M_REG(0xaa, 0x01, 0x30)
#define TAS5825M_REG_DSP_EQ_BQ_2_LEFT                       TAS5825M_REG(0xaa, 0x01, 0x44)
#define TAS5825M_REG_DSP_EQ_BQ_3_LEFT                       TAS5825M_REG(0xaa, 0x01, 0x58)
#define TAS5825M_REG_DSP_EQ_BQ_4_LEFT                       TAS5825M_REG(0xaa, 0x01, 0x6c)
#define TAS5825M_REG_DSP_EQ_BQ_5_LEFT                       TAS5825M_REG(0xaa, 0x02, 0x08)
#define TAS5825M_REG_DSP_EQ_BQ_6_LEFT                       TAS5825M_REG(0xaa, 0x02, 0x1c)
#define TAS5825M_REG_DSP_EQ_BQ_7_LEFT                       TAS5825M_REG(0xaa, 0x02, 0x30)
#define TAS5825M_REG_DSP_EQ_BQ_8_LEFT                       TAS5825M_REG(0xaa, 0x02, 0x44)
#define TAS5825M_REG_DSP_EQ_BQ_9_LEFT                       TAS5825M_REG(0xaa, 0x02, 0x58)
#define TAS5825M_REG_DSP_EQ_BQ_10_LEFT                      TAS5825M_REG(0xaa, 0x02, 0x6c)
#define TAS5825M_REG_DSP_EQ_BQ_11_LEFT                      TAS5825M_REG(0xaa, 0x03, 0x08)
#define TAS5825M_REG_DSP_EQ_BQ_12_LEFT                      TAS5825M_REG(0xaa, 0x03, 0x1c)
#define TAS5825M_REG_DSP_EQ_BQ_13_LEFT                      TAS5825M_REG(0xaa, 0x03, 0x30)
#define TAS5825M_REG_DSP_EQ_BQ_14_LEFT                      TAS5825M_REG(0xaa, 0x03, 0x44)
#define TAS5825M_REG_DSP_EQ_BQ_15_LEFT                      TAS5825M_REG(0xaa, 0x03, 0x58)

#define TAS5825M_REG_DSP_EQ_BQ_1_RIGHT                      TAS5825M_REG(0xaa, 0x03, 0x6c)
#define TAS5825M_REG_DSP_EQ_BQ_2_RIGHT                      TAS5825M_REG(0xaa, 0x04, 0x08)
#define TAS5825M_REG_DSP_EQ_BQ_3_RIGHT                      TAS5825M_REG(0xaa, 0x04, 0x1c)
#define TAS5825M_REG_DSP_EQ_BQ_4_RIGHT                      TAS5825M_REG(0xaa, 0x04, 0x30)
#define TAS5825M_REG_DSP_EQ_BQ_5_RIGHT                      TAS5825M_REG(0xaa, 0x04, 0x44)
#define TAS5825M_REG_DSP_EQ_BQ_6_RIGHT                      TAS5825M_REG(0xaa, 0x04, 0x58)
#define TAS5825M_REG_DSP_EQ_BQ_7_RIGHT                      TAS5825M_REG(0xaa, 0x04, 0x6c)
#define TAS5825M_REG_DSP_EQ_BQ_8_RIGHT                      TAS5825M_REG(0xaa, 0x05, 0x08)
#define TAS5825M_REG_DSP_EQ_BQ_9_RIGHT                      TAS5825M_REG(0xaa, 0x05, 0x1c)
#define TAS5825M_REG_DSP_EQ_BQ_10_RIGHT                     TAS5825M_REG(0xaa, 0x05, 0x30)
#define TAS5825M_REG_DSP_EQ_BQ_11_RIGHT                     TAS5825M_REG(0xaa, 0x05, 0x44)
#define TAS5825M_REG_DSP_EQ_BQ_12_RIGHT                     TAS5825M_REG(0xaa, 0x05, 0x58)
#define TAS5825M_REG_DSP_EQ_BQ_13_RIGHT                     TAS5825M_REG(0xaa, 0x05, 0x6c)
#define TAS5825M_REG_DSP_EQ_BQ_14_RIGHT                     TAS5825M_REG(0xaa, 0x06, 0x08)
#define TAS5825M_REG_DSP_EQ_BQ_15_RIGHT                     TAS5825M_REG(0xaa, 0x06, 0x1c)

#endif
