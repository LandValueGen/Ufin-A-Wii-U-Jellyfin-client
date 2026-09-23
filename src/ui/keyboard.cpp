#include "keyboard.h"
#include "../text_utf.h"

#include <coreinit/debug.h>
#include <coreinit/filesystem.h>
#include <coreinit/memdefaultheap.h>
#include <nn/swkbd.h>
#include <vpad/input.h>
#include <whb/gfx.h>
#include <whb/proc.h>

namespace ui {

bool promptKeyboard(const char16_t* hint, std::string& out, std::string& error) {
    out.clear();
    error.clear();

    if (!WHBGfxInit()) {
        error = "WHBGfxInit failed";
        return false;
    }
    FSInit(); // safe to call again; swkbd loads its data through an FS client

    FSClient* fsClient = (FSClient*)MEMAllocFromDefaultHeap(sizeof(FSClient));
    void* workMemory = MEMAllocFromDefaultHeap(nn::swkbd::GetWorkMemorySize(0));
    if (!fsClient || !workMemory) {
        if (fsClient) MEMFreeToDefaultHeap(fsClient);
        if (workMemory) MEMFreeToDefaultHeap(workMemory);
        WHBGfxShutdown();
        error = "not enough memory for the keyboard";
        return false;
    }
    FSAddClient(fsClient, FS_ERROR_FLAG_NONE);

    nn::swkbd::CreateArg createArg;
    createArg.regionType = nn::swkbd::RegionType::Europe;
    createArg.workMemory = workMemory;
    createArg.fsClient = fsClient;
    bool created = nn::swkbd::Create(createArg);

    bool confirmed = false;
    if (!created) {
        error = "nn::swkbd::Create failed";
    } else {
        // Ufin doesn't initialise AX outside playback; keep swkbd quiet.
        nn::swkbd::MuteAllSound(true);

        nn::swkbd::AppearArg appearArg;
        appearArg.keyboardArg.configArg.languageType = nn::swkbd::LanguageType::English;
        appearArg.inputFormArg.hintText = hint;
        if (!nn::swkbd::AppearInputForm(appearArg)) {
            error = "nn::swkbd::AppearInputForm failed";
        } else {
            bool done = false;
            while (!done && WHBProcIsRunning()) {
                VPADStatus vpad = {};
                VPADReadError vpadError;
                VPADRead(VPAD_CHAN_0, &vpad, 1, &vpadError);
                VPADGetTPCalibratedPoint(VPAD_CHAN_0, &vpad.tpNormal, &vpad.tpNormal);

                nn::swkbd::ControllerInfo controllerInfo;
                controllerInfo.vpad = &vpad;
                controllerInfo.kpad[0] = nullptr;
                controllerInfo.kpad[1] = nullptr;
                controllerInfo.kpad[2] = nullptr;
                controllerInfo.kpad[3] = nullptr;
                nn::swkbd::Calc(controllerInfo);

                if (nn::swkbd::IsNeedCalcSubThreadFont()) nn::swkbd::CalcSubThreadFont();
                if (nn::swkbd::IsNeedCalcSubThreadPredict()) nn::swkbd::CalcSubThreadPredict();

                if (nn::swkbd::IsDecideOkButton(nullptr)) {
                    out = trimSpaces(utf16ToUtf8(nn::swkbd::GetInputFormString()));
                    confirmed = !out.empty();
                    nn::swkbd::DisappearInputForm();
                    done = true;
                } else if (nn::swkbd::IsDecideCancelButton(nullptr)) {
                    nn::swkbd::DisappearInputForm();
                    done = true;
                }

                WHBGfxBeginRender();
                WHBGfxBeginRenderTV();
                WHBGfxClearColor(0.06f, 0.07f, 0.09f, 1.0f);
                nn::swkbd::DrawTV();
                WHBGfxFinishRenderTV();
                WHBGfxBeginRenderDRC();
                WHBGfxClearColor(0.06f, 0.07f, 0.09f, 1.0f);
                nn::swkbd::DrawDRC();
                WHBGfxFinishRenderDRC();
                WHBGfxFinishRender();
            }
        }
        nn::swkbd::Destroy();
    }

    FSDelClient(fsClient, FS_ERROR_FLAG_NONE);
    MEMFreeToDefaultHeap(fsClient);
    MEMFreeToDefaultHeap(workMemory);
    WHBGfxShutdown();

    OSReport("Ufin: keyboard %s (%u bytes)\n", confirmed ? "confirmed" : "cancelled", (unsigned)out.size());
    return confirmed;
}

} // namespace ui
