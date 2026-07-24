module.exports = [
  {
    "type": "heading",
    "defaultValue": "Lavalamp"
  },
  {
    "type": "text",
    "defaultValue": "Choose the appearance of the watchface."
  },
  {
    "type": "section",
    "items": [
      {
        "type": "heading",
        "defaultValue": "Display"
      },
      {
        "type": "toggle",
        "messageKey": "ShowValues",
        "defaultValue": true,
        "label": "Show values",
        "description": "Turn this off to use the original blob-only style."
      }
    ]
  },
  {
    "type": "section",
    "items": [
      {
        "type": "heading",
        "defaultValue": "Colors"
      },
      {
        "type": "color",
        "messageKey": "BackgroundColor",
        "defaultValue": "0xFFFFFF",
        "label": "Background"
      },
      {
        "type": "color",
        "messageKey": "BlobColor",
        "defaultValue": "0x000000",
        "label": "Blobs"
      },
      {
        "type": "color",
        "messageKey": "ValueColor",
        "defaultValue": "0xFFFFFF",
        "label": "Numbers"
      }
    ]
  },
  {
    "type": "submit",
    "defaultValue": "Save"
  }
];
