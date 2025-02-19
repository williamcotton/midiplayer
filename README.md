# MidiPlayer

<img src="https://github.com/user-attachments/assets/c1abc783-717e-4195-beb0-29d591039fe0" width="200">

## iOS MIDI File

```bash
$ xcrun simctl get_app_container booted com.yourcompany.MidiPlayer data
/Users/administrator/Library/Developer/CoreSimulator/Devices/50CEA401-E138-4CBE-96C0-F56814A1E7D4/data/Containers/Data/Application/784697CE-999C-48DE-AE5E-70E08B1F2166
$ cd /Users/administrator/Library/Developer/CoreSimulator/Devices/50CEA401-E138-4CBE-96C0-F56814A1E7D4/data/Containers/Data/Application/784697CE-999C-48DE-AE5E-70E08B1F2166
$ tree .
.
├── Documents
│   └── Triads~Minor~4-4~i_-VII_iv_V_i_-VII_iv_V.mid
├── Library
│   ├── Caches
│   │   └── com.yourcompany.MidiPlayer
│   │       ├── com.apple.metal
│   │       │   ├── functions.data
│   │       │   ├── functions.list
│   │       │   ├── libraries.data
│   │       │   └── libraries.list
│   │       └── com.apple.metalfe
│   ├── Preferences
│   ├── Saved Application State
│   │   └── com.yourcompany.MidiPlayer.savedState
│   │       └── KnownSceneSessions
│   │           └── data.data
│   └── SplashBoard
│       └── Snapshots
│           ├── com.yourcompany.MidiPlayer - {DEFAULT GROUP}
│           │   ├── 153A1248-EE94-4F0C-80F4-C53E2B58EA6C@2x.ktx
│           │   ├── 1C7B5A4F-51C4-4B7E-B9F3-410A3BC156F0@2x.ktx
│           │   ├── 60E53EB2-CD5E-4477-A627-333E99C919CE@2x.ktx
│           │   ├── A961EFE3-D8EC-417B-A320-BE218659A74D@2x.ktx
│           │   └── downscaled
│           └── sceneID:com.yourcompany.MidiPlayer-default
│               ├── 51AFFF25-84E4-496F-992F-2EE6BC3501CA@2x.ktx
│               ├── EA01C8A0-6220-4970-8DE1-C541312CF93F@2x.ktx
│               └── downscaled
│                   ├── 04DA6754-7EBB-474E-BAD5-0A68E39D1306@2x.ktx
│                   └── C0554629-21B9-45D0-87D3-365ABBC1C2FD@2x.ktx
├── SystemData
└── tmp

19 directories, 14 files
```

So copy the MIDI file into the Documents directory. This has to be done for each simulator. It will persist across app restarts.


## Android MIDI File

View > Tool Windows > Devive Explorer

Navigate to: /storage/emulated/0/Download

Add New File, Choose MIDI File
