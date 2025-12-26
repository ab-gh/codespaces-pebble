module.exports = [
  {
    "type": "heading",
    "defaultValue": "Sliding Text++ Settings"
  },
  {
    "type": "text",
    "defaultValue": "View weather and GPS status, and manually refresh data."
  },
  {
    "type": "section",
    "items": [
      {
        "type": "heading",
        "defaultValue": "Status Information"
      },
      {
        "type": "text",
        "defaultValue": "Check the app logs for current weather and GPS status. Stats are logged to the console when you open this page."
      }
    ]
  },
  {
    "type": "section",
    "items": [
      {
        "type": "heading",
        "defaultValue": "Manual Refresh"
      },
      {
        "type": "submit",
        "id": "refresh-weather",
        "defaultValue": "Refresh Weather Now"
      },
      {
        "type": "submit",
        "id": "refresh-gps",
        "defaultValue": "Refresh GPS Location"
      }
    ]
  },
  {
    "type": "section",
    "items": [
      {
        "type": "heading",
        "defaultValue": "Info"
      },
      {
        "type": "text",
        "defaultValue": "Weather updates every 15 minutes"
      },
      {
        "type": "text",
        "defaultValue": "GPS cached for 3 hours"
      }
    ]
  }
];
