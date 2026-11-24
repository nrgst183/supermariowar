#include "SoundOptionsMenu.h"

#include "FileList.h"
#include "GameValues.h"
#include "ResourceManager.h"
#include "ui/MI_AnnouncerField.h"
#include "ui/MI_Button.h"
#include "ui/MI_Image.h"
#include "ui/MI_PacksField.h"
#include "ui/MI_PlaylistField.h"
#include "ui/MI_SelectField.h"
#include "ui/MI_SliderField.h"
#include "ui/MI_Text.h"
#include "ui/SettingsPage.h"

UI_SoundOptionsMenu::UI_SoundOptionsMenu()
    : UI_Menu()
{
    SettingsPage page(7);

    miSoundVolumeField = new MI_SliderField(&rm->spr_selectfield, &rm->menu_slider_bar, page.x(), page.rowY(), "Sound Volume", SettingsPage::ROW_W, SettingsPage::LABEL_W, SettingsPage::ROW_W - 16);
    miSoundVolumeField->add("Off", 0);
    miSoundVolumeField->add("1", 16);
    miSoundVolumeField->add("2", 32);
    miSoundVolumeField->add("3", 48);
    miSoundVolumeField->add("4", 64);
    miSoundVolumeField->add("5", 80);
    miSoundVolumeField->add("6", 96);
    miSoundVolumeField->add("7", 112);
    miSoundVolumeField->add("Max", 128);
    miSoundVolumeField->setOutputPtr(&game_values.soundvolume);
    miSoundVolumeField->setCurrentValue(game_values.soundvolume);
    miSoundVolumeField->allowWrap(false);
    miSoundVolumeField->setItemChangedCode(MENU_CODE_SOUND_VOLUME_CHANGED);
    page.add(miSoundVolumeField, "How loud the sound effects are");

    miMusicVolumeField = new MI_SliderField(&rm->spr_selectfield, &rm->menu_slider_bar, page.x(), page.rowY(), "Music Volume", SettingsPage::ROW_W, SettingsPage::LABEL_W, SettingsPage::ROW_W - 16);
    miMusicVolumeField->add("Off", 0);
    miMusicVolumeField->add("1", 16);
    miMusicVolumeField->add("2", 32);
    miMusicVolumeField->add("3", 48);
    miMusicVolumeField->add("4", 64);
    miMusicVolumeField->add("5", 80);
    miMusicVolumeField->add("6", 96);
    miMusicVolumeField->add("7", 112);
    miMusicVolumeField->add("Max", 128);
    miMusicVolumeField->setOutputPtr(&game_values.musicvolume);
    miMusicVolumeField->setCurrentValue(game_values.musicvolume);
    miMusicVolumeField->allowWrap(false);
    miMusicVolumeField->setItemChangedCode(MENU_CODE_MUSIC_VOLUME_CHANGED);
    page.add(miMusicVolumeField, "How loud the music is");

    miPlayNextMusicField = new MI_SelectField<bool>(&rm->spr_selectfield, page.x(), page.rowY(), "Next Music", SettingsPage::ROW_W, SettingsPage::LABEL_W);
    miPlayNextMusicField->add("Off", false);
    miPlayNextMusicField->add("On", true);
    miPlayNextMusicField->setOutputPtr(&game_values.playnextmusic);
    miPlayNextMusicField->setCurrentValue(game_values.playnextmusic ? 1 : 0);
    page.add(miPlayNextMusicField, "Next song when one ends, or repeat it");

    miAnnouncerField = new MI_AnnouncerField(&rm->spr_selectfield, page.x(), page.rowY(), "Announcer", SettingsPage::ROW_W, SettingsPage::LABEL_W, announcerlist);
    page.add(miAnnouncerField, "The voice that calls out the action");

    miSoundPackField = new MI_PacksField(&rm->spr_selectfield, page.x(), page.rowY(), "Sound Pack", SettingsPage::ROW_W, SettingsPage::LABEL_W, soundpacklist, MENU_CODE_SOUND_PACK_CHANGED);
    page.add(miSoundPackField, "The sound effects");

    miPlaylistField = new MI_PlaylistField(&rm->spr_selectfield, page.x(), page.rowY(), "Game Music Pack", SettingsPage::ROW_W, SettingsPage::LABEL_W);
    page.add(miPlaylistField, "Music played in matches");

    miWorldMusicField = new MI_SelectField<short>(&rm->spr_selectfield, page.x(), page.rowY(), "World Music Pack", SettingsPage::ROW_W, SettingsPage::LABEL_W);

    int iCurrentMusic = worldmusiclist->currentIndex();
    worldmusiclist->setCurrent(0);
    for (size_t iMusic = 0; iMusic < worldmusiclist->count(); iMusic++) {
        miWorldMusicField->add(worldmusiclist->currentName(), iMusic);
        worldmusiclist->next();
    }
    miWorldMusicField->setCurrentValue(iCurrentMusic);
    worldmusiclist->setCurrent(iCurrentMusic);
    miWorldMusicField->setItemChangedCode(MENU_CODE_WORLD_MUSIC_CHANGED);
    page.add(miWorldMusicField, "Music played on world maps");

    page.finish(*this, "Sound Options", MENU_CODE_BACK_TO_OPTIONS_MENU);
};

short UI_SoundOptionsMenu::GetCurrentWorldMusicID() const
{
    return miWorldMusicField->currentValue();
}
