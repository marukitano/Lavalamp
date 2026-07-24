module.exports = [
  {
    "type": "heading",
    "defaultValue": "Lavalamp"
  },
  {
    "type": "text",
    "defaultValue": "Choose the colors used by the watchface."
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
