#include "input.h"
#include "uicontrol.h"

#include <memory>
#include <string>

class gfxSprite;
class MI_Image;
class MI_Text;
class MI_Button;
template<typename T> class MI_SelectField;

class MI_InputControlField : public UI_Control
{
    public:
        MI_InputControlField(gfxSprite * nspr, short x, short y, std::string name, short width, short indent);
        virtual ~MI_InputControlField();

        void Draw() override;
        MenuCodeEnum SendInput(CPlayerInput * playerInput) override;
        void SetKey(SDL_Keycode * iSetKey, SDL_Keycode key, SDL_JoystickID device);

        MenuCodeEnum Modify(bool modify) override;
        Box FocusBox() const override {
            //(the pipe, as a select field's: its sprite has 8 empty pixels at each end)
            return {static_cast<short>(m_pos.x + 8), m_pos.y, static_cast<short>(iWidth - 16), 32};
        }
        Box ValueBox() const override {
            return {static_cast<short>(m_pos.x + iIndent - 12), m_pos.y, static_cast<short>(iWidth - iIndent + 4), 32};
        }
        bool ChangingValue() const override { return false; }    //(waiting for a key: no left/right to change)

    void SetDevice(SDL_JoystickID device) {
        iDevice = device;
    }
    void SetKey(SDL_Keycode * key) {
        iKey = key;
    }
    void SetType(short type) {
        iType = type;
    }
    void SetKeyIndex(short keyindex) {
        iKeyIndex = keyindex;
    }
    void SetPlayerIndex(short playerindex) {
        iPlayerIndex = playerindex;
    }

    private:

        gfxSprite * spr;
        const std::string szName;

        short iWidth, iIndent;

        SDL_JoystickID iDevice;
        SDL_Keycode * iKey;
        short iType;
        short iKeyIndex;
        short iPlayerIndex;

        static const char * Joynames[30];
};

class MI_InputControlContainer : public UI_Control
{
    public:

        MI_InputControlContainer(gfxSprite * spr_button, short x, short y, short playerID);
        virtual ~MI_InputControlContainer();

        void Update() override;
        void Draw() override;

        MenuCodeEnum SendInput(CPlayerInput * playerInput) override;
        MenuCodeEnum Modify(bool modify) override;
        bool ChangingValue() const override;    //(its own menu's)

        void SetPlayer(short iPlayerID);

        void UpdateDeviceKeys(SDL_JoystickID iDevice);

    private:

        void SetVisibleInputFields();
        void refreshDeviceList();

        short iPlayerID;
        SDL_JoystickID iDevice;
        short iSelectedInputType;

        std::unique_ptr<UI_Menu> mInputMenu;

        MI_Text * miText;
        MI_SelectField<SDL_JoystickID>* miDeviceSelectField;
        MI_Button * miInputTypeButton;
        MI_InputControlField * miGameInputControlFields[NUM_KEYS];
        MI_InputControlField * miMenuInputControlFields[NUM_KEYS];

};
