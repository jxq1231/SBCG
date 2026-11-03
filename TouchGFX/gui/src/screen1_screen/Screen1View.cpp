/**
 * @file Screen1View.cpp
 * @brief 在原测距状态页上增加四个逻辑页面和固定底部导航。
 * @details 原十三项文本继续按版本绑定；击掌显示独立权威结果，波形只绘CPU副本，设置页保留二次确认。
 * @note 仅前台定容控件；不读取PCM或直接改测距参数。触控失效时取消按键，业务动作由App处理。
 */
#include <gui/screen1_screen/Screen1View.hpp>
#include "UI/touchgfx_service.h"
#include "BSP/bsp_touch.h"
#include <touchgfx/Color.hpp>
#include <texts/TextKeysAndLanguages.hpp>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <touchgfx/hal/HAL.hpp>
#include <touchgfx/lcd/LCD.hpp>
extern "C" {
#include "app_main.h"
}

/** @brief 组员信息；Verdana字体无中文字形，姓名使用拼音以保证可显示。 */
static const char kGroupMembers[] = "SBCG PROJECT TEAM";

/** @brief 清空尚未接收的CPU波形副本。@return 构造无返回值。 */
WaveformWidget::WaveformWidget() : frame{}, haveFrame(false) {}

/** @brief 原位覆盖唯一快照，使用服务帧号门控重绘。@return 无返回值。 */
void WaveformWidget::refresh()
{
    const uint32_t previous = frame.frame_seq;
    WaveformService_GetSnapshot(&frame);
    if (!haveFrame || previous != frame.frame_seq) { invalidate(); }
    haveFrame = true;
}

/** @brief 返回完全覆盖背景的局部矩形。@return 不透明区域。 */
touchgfx::Rect WaveformWidget::getSolidRect() const
{
    return touchgfx::Rect(0, 0, getWidth(), getHeight());
}

/** @brief 使用LCD填充裁剪后的细条，连接真实PCM点；不分配Canvas。@param area 局部脏矩形。@return 无返回值。 */
void WaveformWidget::draw(const touchgfx::Rect& area) const
{
    touchgfx::Rect clip = area & getSolidRect();
    if (clip.isEmpty()) { return; }
    translateRectToAbsolute(clip);
    auto& lcd = touchgfx::HAL::lcd();
    lcd.fillRect(clip, touchgfx::Color::getColorFromRGB(11, 18, 32));
    const auto origin = getAbsoluteRect();
    touchgfx::Rect axis(origin.x, origin.y + getHeight()/2, getWidth(), 1);
    axis &= clip;
    if (!axis.isEmpty()) { lcd.fillRect(axis, touchgfx::Color::getColorFromRGB(55, 77, 94)); }
    if (!frame.valid || frame.point_count < 2U) { return; }
    // 服务保证point_count<=256。只改变显示比例；至少256 PCM，避免微小噪声铺满画面。
    int32_t peak_pcm = 256;
    for (uint16_t i=0U; i<frame.point_count; ++i)
    {
        const int32_t sample = frame.points[i];
        peak_pcm = std::max(peak_pcm, sample<0 ? -sample : sample);
    }
    const int32_t display_span_pcm = 2*peak_pcm;
    // 每像素一条竖线连接相邻插值点，不改变源样本、时间窗或横坐标。
    int16_t previous_y = static_cast<int16_t>((getHeight()-1)/2 -
        static_cast<int32_t>(frame.points[0])*(getHeight()-1)/display_span_pcm);
    for (int16_t x=0; x<getWidth(); ++x)
    {
        const uint32_t position = static_cast<uint32_t>(x)*(frame.point_count-1U);
        const uint32_t index = position/(getWidth()-1);
        const uint32_t fraction = position%(getWidth()-1);
        int32_t sample = frame.points[index];
        if (index+1U < frame.point_count)
            sample += (static_cast<int32_t>(frame.points[index+1U])-sample)*
                static_cast<int32_t>(fraction)/(getWidth()-1);
        const int16_t y = static_cast<int16_t>((getHeight()-1)/2-sample*(getHeight()-1)/display_span_pcm);
        touchgfx::Rect line(origin.x+x, origin.y+std::min(y,previous_y), 1,
            static_cast<int16_t>(std::max(y,previous_y)-std::min(y,previous_y)+1));
        line &= clip;
        if (!line.isEmpty()) { lcd.fillRect(line, touchgfx::Color::getColorFromRGB(45, 212, 191)); }
        previous_y = y;
    }
}

/**
 * @brief 有界转换ASCII服务文本，并在内容变化时通知控件重绘。
 * @param widget 前台拥有的单通配符控件，生命周期覆盖此调用。
 * @param buffer 生成基类拥有的非空Unicode数组，长度N包含终止符。
 * @param text 非NULL、NUL结尾的服务副本文本，不保留指针。
 * @return 无返回值；仅修改对应缓冲及失效区域。
 */
template <std::size_t N>
static void UpdateText(touchgfx::TextAreaWithOneWildcard& widget,
                       touchgfx::Unicode::UnicodeChar (&buffer)[N], const char* text)
{
    touchgfx::Unicode::UnicodeChar replacement[N] = {};
    touchgfx::Unicode::fromUTF8(reinterpret_cast<const uint8_t*>(text), replacement,
                               static_cast<uint16_t>(N));
    replacement[N - 1U] = 0;
    if (std::memcmp(buffer, replacement, sizeof buffer) != 0)
    {
        std::memcpy(buffer, replacement, sizeof buffer);
        widget.invalidate();
    }
}

/** @brief 初始化本视图的数据版本门控；尚未读取业务。@return 无返回值。 */
Screen1View::Screen1View() : lastGeneration(0), haveGeneration(false),
    activePage(RANGE), pressedControl(-1), confirmation(CALIBRATION_ACTION_NONE),
    confirmationSample{}, settingEnabled{false,false,false}, requestRejected(false)
{
    configureNavigation();
}

/** @brief 设置固定位置、现有通配符字体及前台拥有的缓冲，不申请堆内存。@return 无返回值。 */
void Screen1View::configureNavigation()
{
    const auto normal = touchgfx::Color::getColorFromRGB(23, 34, 52);
    const auto muted = touchgfx::Color::getColorFromRGB(146, 164, 184);
    const auto white = touchgfx::Color::getColorFromRGB(232, 240, 248);
    const auto accent = touchgfx::Color::getColorFromRGB(45, 212, 191);
    // 组员信息固定显示在顶部状态行下沿，不占用各页面板区域。
    roleText.setPosition(110, 8, 180, 24);
    header.setPosition(319, 6, 150, 20);
    groupLine.setPosition(110, 33, 358, 12);
    groupLine.setTypedText(touchgfx::TypedText(T_T_DETAILTEXT));
    groupLine.setWildcard(groupLineBuffer);
    groupLine.setColor(muted);
    groupLineBuffer[0] = 0;
    UpdateText(groupLine, groupLineBuffer, kGroupMembers);
    add(groupLine);
    measurePanel.setPosition(12, 46, 216, 104);
    syncPanel.setPosition(240, 46, 228, 104);
    waiting.setVisible(false);
    precision.setVisible(false);
    footer.setVisible(false);
    networkPanel.setPosition(12, 156, 140, 71);
    tempPanel.setPosition(164, 156, 140, 71);
    audioPanel.setPosition(316, 156, 152, 71);
    networkTitle.setPosition(24, 161, 115, 18);
    networkText.setPosition(24, 185, 125, 28);
    sourceText.setPosition(176, 161, 124, 18);
    temperatureText.setPosition(176, 180, 124, 26);
    speedText.setPosition(176, 207, 124, 18);
    audioTitle.setPosition(328, 157, 126, 18);
    audioText.setPosition(328, 175, 130, 23);
    detailText.setPosition(328, 198, 130, 15);
    eventsText.setPosition(328, 213, 130, 14);

    pagePanel.setPosition(12, 46, 456, 176);
    pagePanel.setColor(normal);
    add(pagePanel);
    waveform.setPosition(24, 133, 432, 78);
    add(waveform);
    pageTitle.setPosition(24, 58, 432, 29);
    pageTitle.setTypedText(touchgfx::TypedText(T_T_SYNCTEXT));
    pageTitle.setWildcard(pageTitleBuffer);
    pageTitle.setColor(white);
    pageTitleBuffer[0] = 0;
    add(pageTitle);
    for (uint8_t i = 0; i < 5U; ++i)
    {
        pageLines[i].setPosition(24, static_cast<int16_t>(96 + i * 23), 432, 22);
        pageLines[i].setTypedText(touchgfx::TypedText(T_T_ROLETEXT));
        pageLines[i].setWildcard(pageLineBuffers[i]);
        pageLines[i].setColor(i == 0U ? accent : muted);
        pageLineBuffers[i][0] = 0;
        add(pageLines[i]);
    }
    const char* settingText[] = {"CALIBRATE", "ENABLE", "CLEAR"};
    for (uint8_t i = 0; i < 3U; ++i)
    {
        settingTiles[i].setPosition(static_cast<int16_t>(24 + i * 146), 185, 138, 34);
        settingTiles[i].setColor(touchgfx::Color::getColorFromRGB(35, 44, 60));
        add(settingTiles[i]);
        settingLabels[i].setPosition(static_cast<int16_t>(36 + i * 146), 191, 114, 23);
        settingLabels[i].setTypedText(touchgfx::TypedText(T_T_ROLETEXT));
        settingLabels[i].setWildcard(settingBuffers[i]);
        settingLabels[i].setColor(muted);
        settingBuffers[i][0] = 0;
        UpdateText(settingLabels[i], settingBuffers[i], settingText[i]);
        add(settingLabels[i]);
    }
    navigationBackground.setPosition(0, 228, 480, 44);
    navigationBackground.setColor(touchgfx::Color::getColorFromRGB(11, 18, 32));
    add(navigationBackground);
    const char* labels[] = {"RANGE", "CLAP", "WAVE", "SETUP"};
    for (uint8_t i = 0; i < PAGE_COUNT; ++i)
    {
        navigationTiles[i].setPosition(static_cast<int16_t>(4 + i * 120), 232, 112, 36);
        add(navigationTiles[i]);
        navigationLabels[i].setPosition(static_cast<int16_t>(16 + i * 120), 239, 92, 23);
        navigationLabels[i].setTypedText(touchgfx::TypedText(T_T_ROLETEXT));
        navigationLabels[i].setWildcard(navigationBuffers[i]);
        navigationBuffers[i][0] = 0;
        UpdateText(navigationLabels[i], navigationBuffers[i], labels[i]);
        add(navigationLabels[i]);
    }
    showPage(RANGE);
}

/** @brief 切换可见控件；业务文本在隐藏期间仍按原服务版本更新。@param page 合法页面枚举。@return 无返回值。 */
void Screen1View::showPage(Page page)
{
    activePage = page;
    pressedControl = -1;
    confirmation = CALIBRATION_ACTION_NONE;
    requestRejected = false;
    touchgfx::Drawable* rangeWidgets[] = {&measurePanel, &syncPanel, &networkPanel,
        &tempPanel, &audioPanel, &distanceTitle, &distanceText, &directionText,
        &qualityText, &syncTitle, &syncText, &offsetText, &networkTitle, &networkText,
        &sourceText, &temperatureText, &speedText, &audioTitle, &audioText,
        &detailText, &eventsText};
    for (auto* widget : rangeWidgets) { widget->setVisible(page == RANGE); }
    pagePanel.setVisible(page != RANGE);
    waveform.setVisible(page == WAVE);
    pageTitle.setVisible(page != RANGE);
    for (uint8_t i=0;i<5U;++i) {
        pageLines[i].setVisible(page != RANGE);
        pageLines[i].setPosition(24,static_cast<int16_t>(96+i*23),432,22);
        pageLines[i].setTypedText(touchgfx::TypedText(T_T_ROLETEXT));
    }
    for (uint8_t i = 0; i < 3U; ++i)
    {
        settingTiles[i].setVisible(page == SETUP);
        settingLabels[i].setVisible(page == SETUP);
    }
    const char* title = "";
    const char* lines[5] = {"", "", "", "", ""};
    if (page == CLAP)
    {
        title = "CLAP RANGING";
    }
    else if (page == WAVE)
    {
        title = "MICROPHONE WAVEFORM";
        for (uint8_t i=2; i<5U; ++i) { pageLines[i].setVisible(false); }
        pageLines[0].setPosition(24, 90, 432, 20);
        pageLines[1].setPosition(24, 112, 432, 18);
        pageLines[1].setTypedText(touchgfx::TypedText(T_T_DIRECTIONTEXT));
    }
    else if (page == SETUP)
    {
        title = "CALIBRATION SETTINGS";
    }
    UpdateText(pageTitle, pageTitleBuffer, title);
    for (uint8_t i = 0; i < 5U; ++i)
    { UpdateText(pageLines[i], pageLineBuffers[i], lines[i]); }
    if(page == SETUP) { refreshCalibration(); }
    if(page == CLAP) { refreshClap(); }
    if(page == WAVE) { refreshWaveform(); }
    refreshNavigation();
    invalidate();
}

/** @brief 为选中、按下和普通导航项分别赋色并局部重绘。@return 无返回值。 */
void Screen1View::refreshNavigation()
{
    for (uint8_t i = 0; i < PAGE_COUNT; ++i)
    {
        const bool selected = i == activePage;
        const bool pressed = i == pressedControl;
        navigationTiles[i].setColor(selected || pressed ?
            touchgfx::Color::getColorFromRGB(27, 71, 78) :
            touchgfx::Color::getColorFromRGB(23, 34, 52));
        navigationLabels[i].setColor(selected || pressed ?
            touchgfx::Color::getColorFromRGB(45, 212, 191) :
            touchgfx::Color::getColorFromRGB(146, 164, 184));
        navigationTiles[i].invalidate();
        navigationLabels[i].invalidate();
    }
}

/** @brief 按实际矩形命中导航，间隔和屏外返回-1。@param x 横坐标像素。@param y 纵坐标像素。@return 导航索引或-1。 */
int8_t Screen1View::navigationAt(int16_t x, int16_t y)
{
    if (x < 4 || x >= 476 || y < 232 || y >= 268) { return -1; }
    const int16_t index = x / 120;
    const int16_t local = x % 120;
    return local >= 4 && local < 116 ? static_cast<int8_t>(index) : -1;
}

/** @brief 屏幕事件只驱动导航；异常读取造成的框架释放不视为确认。@param event 本次借用的全屏点击。@return 无返回值。 */
void Screen1View::handleClickEvent(const touchgfx::ClickEvent& event)
{
    if (event.getType() == touchgfx::ClickEvent::PRESSED)
    {
        pressedControl = navigationAt(event.getX(), event.getY());
        const int8_t setting=settingAt(event.getX(),event.getY());
        if(pressedControl<0 && setting>=0)pressedControl=static_cast<int8_t>(PAGE_COUNT+setting);
        refreshNavigation();
        if(activePage==SETUP)refreshCalibration();
    }
    else
    {
        BSP_TouchSnapshot touch;
        BSP_Touch_GetSnapshot(&touch);
        const int8_t selected = pressedControl;
        pressedControl = -1;
        if (event.getType() == touchgfx::ClickEvent::RELEASED && touch.sample_valid && selected>=0)
        {
            if(selected<PAGE_COUNT && selected==navigationAt(event.getX(),event.getY()))
                showPage(static_cast<Page>(selected));
            else if(selected>=PAGE_COUNT && selected-PAGE_COUNT==settingAt(event.getX(),event.getY()))
                selectSetting(static_cast<uint8_t>(selected-PAGE_COUNT));
        }
        refreshNavigation();
        if(activePage==SETUP)refreshCalibration();
    }
}

/** @brief 拖离原按钮后锁定取消，重新拖回也不恢复。@param event 当前全屏拖动。@return 无返回值。 */
void Screen1View::handleDragEvent(const touchgfx::DragEvent& event)
{
    const int8_t hit=pressedControl<PAGE_COUNT?navigationAt(event.getNewX(),event.getNewY()):
        (settingAt(event.getNewX(),event.getNewY())<0?-1:
         static_cast<int8_t>(PAGE_COUNT+settingAt(event.getNewX(),event.getNewY())));
    if (pressedControl >= 0 && pressedControl != hit)
    { pressedControl=-1;refreshNavigation();if(activePage==SETUP)refreshCalibration(); }
}

/** @brief 进入状态页并尝试装载服务副本；无返回值，前台调用。 */
void Screen1View::setupScreen()
{
    Screen1ViewBase::setupScreen();
    pressedControl = -1;
    haveGeneration = false;
    refreshStatus();
}

/** @brief 退出时保留业务运行，仅执行基类页面清理；无返回值。 */
void Screen1View::tearDownScreen()
{
    Screen1ViewBase::tearDownScreen();
    pressedControl = -1;
    confirmation = CALIBRATION_ACTION_NONE;
}

/** @brief 框架前台tick后检查版本；不将VSYNC频率误当业务更新频率。@return 无返回值。 */
void Screen1View::handleTickEvent()
{
    Screen1ViewBase::handleTickEvent();
    refreshStatus();
    if(activePage==SETUP)refreshCalibration();
    if(activePage==CLAP)refreshClap();
    if(activePage==WAVE)refreshWaveform();
}

/** @brief 读取独立击掌结果；撤值后立即移除旧数字。@return 无返回值，RANGE控件不受影响。 */
void Screen1View::refreshClap()
{
    ClapRangingSnapshot clap;
    App_GetClapSnapshot(&clap);
    char text[64];
    UpdateText(pageLines[0], pageLineBuffers[0], !clap.has_value ? "WAIT / NO CLAP" :
        clap.fresh ? "FRESH / SINGLE CLAP" : "HELD / LAST CLAP");
    if (clap.has_value)
        std::snprintf(text,sizeof text,"DISTANCE %ld MM",static_cast<long>(clap.result.distance_mm));
    else std::snprintf(text,sizeof text,"DISTANCE -- MM");
    UpdateText(pageLines[1],pageLineBuffers[1],text);
    if (clap.has_value)
        std::snprintf(text,sizeof text,"SIDE %s / QUALITY %u",clap.result.direction==1U?"A":
            clap.result.direction==2U?"B":"--",static_cast<unsigned>(clap.result.quality));
    else std::snprintf(text,sizeof text,"SIDE -- / QUALITY --");
    UpdateText(pageLines[2],pageLineBuffers[2],text);
    if (clap.has_value)
        std::snprintf(text,sizeof text,"SOURCE AGE %lu MS",static_cast<unsigned long>(clap.source_age_ms));
    else std::snprintf(text,sizeof text,"WAIT FOR A REAL CLAP");
    UpdateText(pageLines[3],pageLineBuffers[3],text);
    UpdateText(pageLines[4],pageLineBuffers[4],"STANDARD RANGING CONTINUES");
}

/** @brief 可见时取CPU波形并显示实际窗口和同步资格，未同步仍可画本地PCM。@return 无返回值。 */
void Screen1View::refreshWaveform()
{
    waveform.refresh();
    const auto& wave = waveform.snapshot();
    UpdateText(pageLines[0],pageLineBuffers[0],!wave.valid?"WAIT / NO AUDIO":
        wave.synced?"SYNC / COMMON WINDOW":"LOCAL / UNSYNCED");
    char text[64];
    if (wave.period_valid)
        std::snprintf(text,sizeof text,"PERIOD %lu.%03lu HZ / WINDOW %lu MS",
            static_cast<unsigned long>(wave.period_milli_hz/1000U),
            static_cast<unsigned long>(wave.period_milli_hz%1000U),
            static_cast<unsigned long>(wave.window_span_ns/1000000U));
    else std::snprintf(text,sizeof text,"FIXED / WINDOW %lu MS / PCM16",
            static_cast<unsigned long>(wave.window_span_ns/1000000U));
    UpdateText(pageLines[1],pageLineBuffers[1],text);
}

/** @brief 设置页仅命中当前可用按钮，确认页复用前两键。@param x 横坐标像素。@param y 纵坐标像素。@return 索引或-1。 */
int8_t Screen1View::settingAt(int16_t x,int16_t y) const
{
    if(activePage!=SETUP || y<185 || y>=219)return -1;
    for(int8_t i=0;i<3;++i)
        if(settingEnabled[i] && x>=24+i*146 && x<162+i*146)return i;
    return -1;
}

/** @brief 打开动作确认或提交已确认的快照，不直接调用标定/测量写接口。@param index 当前命中的按钮索引。@return 无返回值。 */
void Screen1View::selectSetting(uint8_t index)
{
    if(confirmation!=CALIBRATION_ACTION_NONE)
    {
        if(index==0U)
            requestRejected=!CalibrationControl_Request(confirmation,
                confirmation==CALIBRATION_ACTION_CAPTURE?&confirmationSample:nullptr);
        confirmation=CALIBRATION_ACTION_NONE;
        return;
    }
    CalibrationControlSnapshot control;CalibrationControl_GetSnapshot(&control);
    if(!control.authority)return;
    requestRejected=false;
    if(index==0U && control.sample_available)
    { confirmation=CALIBRATION_ACTION_CAPTURE;confirmationSample=control.sample; }
    else if(index==1U && control.calibration.calibrated)
        confirmation=control.calibration.enabled?CALIBRATION_ACTION_DISABLE:CALIBRATION_ACTION_ENABLE;
    else if(index==2U && control.calibration.calibrated)
        confirmation=CALIBRATION_ACTION_CLEAR;
}

/** @brief 依据实际控制副本刷新设置页，确认阶段冻结用户所见样本身份。@return 无返回值，只更新变化文本/按钮颜色。 */
void Screen1View::refreshCalibration()
{
    CalibrationControlSnapshot control;CalibrationControl_GetSnapshot(&control);
    char lines[4][64] = {};
    const char* labels[3]={"CALIBRATE",control.calibration.enabled?"DISABLE":"ENABLE","CLEAR"};
    settingEnabled[0]=control.authority && control.sample_available;
    settingEnabled[1]=settingEnabled[2]=control.authority && control.calibration.calibrated;
    if(confirmation!=CALIBRATION_ACTION_NONE)
    {
        labels[0]="CONFIRM";labels[1]="CANCEL";labels[2]="--";
        settingEnabled[0]=settingEnabled[1]=true;settingEnabled[2]=false;
        const char* prompt=confirmation==CALIBRATION_ACTION_CAPTURE?"BOARDS AT 500 MM?":
            confirmation==CALIBRATION_ACTION_ENABLE?"ENABLE SAVED CALIBRATION?":
            confirmation==CALIBRATION_ACTION_DISABLE?"DISABLE / KEEP SAVED OFFSET?":"CLEAR SAVED CALIBRATION?";
        std::snprintf(lines[0],sizeof lines[0],"%s",prompt);
        if(confirmation==CALIBRATION_ACTION_CAPTURE)
            std::snprintf(lines[1],sizeof lines[1],"RAW %ld MM / NEW %+ld MM",
                static_cast<long>(confirmationSample.raw_mm),static_cast<long>(500-confirmationSample.raw_mm));
        else std::snprintf(lines[1],sizeof lines[1],"SAVED OFFSET %+ld MM",static_cast<long>(control.calibration.offset_mm));
        std::snprintf(lines[2],sizeof lines[2],"CONFIRM OR CANCEL BELOW");
    }
    else if(control.authority)
    {
        std::snprintf(lines[0],sizeof lines[0],"%s",!control.calibration.calibrated?"NOT CALIBRATED":
            control.calibration.enabled?"SAVED / ENABLED":"SAVED / DISABLED");
        std::snprintf(lines[1],sizeof lines[1],"SAVED %+ld MM / APPLIED %+ld MM",
            static_cast<long>(control.calibration.offset_mm),static_cast<long>(control.applied_offset_mm));
        if(control.sample_available)
            std::snprintf(lines[2],sizeof lines[2],"RAW %ld MM / V%u",static_cast<long>(control.sample.raw_mm),control.applied_version);
        else std::snprintf(lines[2],sizeof lines[2],"NO FRESH TRACKING / V%u",control.applied_version);
    }
    else
    {
        std::snprintf(lines[0],sizeof lines[0],"READ ONLY / CONTROL ON A");
        std::snprintf(lines[1],sizeof lines[1],"APPLIED %+ld MM / V%u",static_cast<long>(control.applied_offset_mm),control.applied_version);
        std::snprintf(lines[2],sizeof lines[2],"USE A BOARD FOR CALIBRATION");
    }
    const char* feedback="REFERENCE 500 MM / RAM ONLY";
    if(requestRejected)feedback="REQUEST BUSY / TRY AGAIN";
    else switch(control.result)
    {
    case CALIBRATION_CONTROL_PENDING:feedback="REQUEST QUEUED";break;
    case CALIBRATION_CONTROL_APPLIED:feedback=control.sample_available?"UPDATED / READY":"UPDATED / NEW SOUND REQUIRED";break;
    case CALIBRATION_CONTROL_A_ONLY:feedback="LAST: CONTROL ON A ONLY";break;
    case CALIBRATION_CONTROL_NO_SAMPLE:feedback="LAST: NO FRESH TRACKING SAMPLE";break;
    case CALIBRATION_CONTROL_SAMPLE_CHANGED:feedback="LAST: SAMPLE CHANGED / TRY AGAIN";break;
    case CALIBRATION_CONTROL_NO_SAVED:feedback="LAST: NO SAVED COEFFICIENT";break;
    case CALIBRATION_CONTROL_OUT_OF_RANGE:feedback="LAST: OFFSET OUT OF RANGE";break;
    default:break;
    }
    std::snprintf(lines[3],sizeof lines[3],"%s",feedback);
    for(uint8_t i=0;i<4U;++i)
    {
        pageLines[i].setPosition(24,static_cast<int16_t>(91+i*24),432,i==3U?18:22);
        pageLines[i].setTypedText(touchgfx::TypedText(i==3U?T_T_DIRECTIONTEXT:T_T_ROLETEXT));
        UpdateText(pageLines[i],pageLineBuffers[i],lines[i]);
    }
    pageLines[4].setVisible(false);
    for(uint8_t i=0;i<3U;++i)
    {
        UpdateText(settingLabels[i],settingBuffers[i],labels[i]);
        const auto tile=touchgfx::Color::getColorFromRGB(settingEnabled[i]?27:35,settingEnabled[i]?71:44,settingEnabled[i]?78:60);
        const auto text=settingEnabled[i]?touchgfx::Color::getColorFromRGB(45,212,191):touchgfx::Color::getColorFromRGB(146,164,184);
        if(settingTiles[i].getColor()!=tile)
        { settingTiles[i].setColor(tile);settingTiles[i].invalidate(); }
        if(settingLabels[i].getColor()!=text)
        { settingLabels[i].setColor(text);settingLabels[i].invalidate(); }
    }
}

/** @brief 从服务复制文本，按字段更新；副本不可用或版本相同则保持原页。@return 无返回值。 */
void Screen1View::refreshStatus()
{
    UI_StatusText text;
    uint32_t generation;
    if (!UI_TouchGFX_GetText(&text, &generation) ||
        (haveGeneration && generation == lastGeneration)) { return; }
    UpdateText(roleText, roleTextBuffer, text.role);
    UpdateText(distanceText, distanceTextBuffer, text.distance);
    UpdateText(directionText, directionTextBuffer, text.direction);
    UpdateText(qualityText, qualityTextBuffer, text.quality);
    UpdateText(syncText, syncTextBuffer, text.sync);
    UpdateText(offsetText, offsetTextBuffer, text.offset);
    UpdateText(networkText, networkTextBuffer, text.network);
    UpdateText(sourceText, sourceTextBuffer, text.temperature_source);
    UpdateText(temperatureText, temperatureTextBuffer, text.temperature);
    UpdateText(speedText, speedTextBuffer, text.speed);
    UpdateText(audioText, audioTextBuffer, text.audio);
    UpdateText(detailText, detailTextBuffer, text.audio_detail);
    UpdateText(eventsText, eventsTextBuffer, text.counts);
    lastGeneration = generation;
    haveGeneration = true;
}
