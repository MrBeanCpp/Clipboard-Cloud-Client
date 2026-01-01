#ifndef TOASTHANDLER_H
#define TOASTHANDLER_H

#include "qdebug.h"
#include <wintoastlib.h>
#include <functional>
#include <QString>

class ToastHandler final : public WinToastLib::IWinToastHandler {
public:
    // 0=确认, 1=取消（按 addAction 顺序）
    std::function<void(int actionIndex)> onAction;
    std::function<void()> onBodyClick;
    std::function<void(WinToastDismissalReason)> onDismiss;
    std::function<void()> onFail;

    void toastActivated() const override {
        if (onBodyClick) onBodyClick();
    }

    void toastActivated(int actionIndex) const override {
        if (onAction) onAction(actionIndex);
    }

    void toastActivated(std::wstring response) const override {
        std::wcout << L"The user replied with: " << response << std::endl;
    }

    void toastDismissed(WinToastDismissalReason state) const override {
        if (onDismiss) onDismiss(state);
    }

    void toastFailed() const override {
        if (onFail) onFail();
    }
};

inline void initWinToast(const QString& appName, const QString& companyName) {
    using namespace WinToastLib;

    if (!WinToast::isCompatible())
        qWarning() << "Warn: Windows version not supported for Toast.";

    WinToast::instance()->setAppName(appName.toStdWString());
    const auto aumi = WinToast::configureAUMI(companyName.toStdWString(), appName.toStdWString());
    WinToast::instance()->setAppUserModelId(aumi);

    if (!WinToast::instance()->initialize())
        qWarning() << "Warn: Could not initialize WinToastLib.";
}

inline void showToastWithHeroImageText(const QString& heroImage, const QString& firstLine, const QString& secondLine = "", std::function<void()> onBodyClick = nullptr) {
    using namespace WinToastLib;

    auto* h = new ToastHandler();
    h->onBodyClick = std::move(onBodyClick);

    WinToastTemplate templ = WinToastTemplate(WinToastTemplate::ImageAndText02);
    templ.setTextField(firstLine.toStdWString(), WinToastTemplate::FirstLine);
    templ.setTextField(secondLine.toStdWString(), WinToastTemplate::SecondLine);
    if (!heroImage.isEmpty())
        templ.setHeroImagePath(heroImage.toStdWString());

    if (WinToast::instance()->showToast(templ, h) < 0)
        qWarning() << "Could not launch your toast notification!";
}

inline void showToastWithActions(const QString& image, const QString& firstLine, const QString& secondLine, const QString& sourceText,
                                 std::function<void(int actionIndex)> onAction, const QString& okText = "Open", const QString& noText = "Cancel") {
    using namespace WinToastLib;

    auto* h = new ToastHandler();
    h->onAction = std::move(onAction);

    WinToastTemplate templ = WinToastTemplate(WinToastTemplate::ImageAndText02);
    templ.setTextField(firstLine.toStdWString(), WinToastTemplate::FirstLine);
    templ.setTextField(secondLine.toStdWString(), WinToastTemplate::SecondLine);
    templ.setAttributionText(sourceText.toStdWString());
    if (!image.isEmpty())
        templ.setImagePath(image.toStdWString());
    templ.addAction(okText.toStdWString());
    templ.addAction(noText.toStdWString());

    if (WinToast::instance()->showToast(templ, h) < 0)
        qWarning() << "Could not launch your toast notification!";
}

#endif // TOASTHANDLER_H
