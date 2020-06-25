// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/tools/print-input-report/devices.h"

#include <fuchsia/input/report/llcpp/fidl.h>
#include <lib/trace/event.h>
#include <zircon/status.h>

#include <ddk/device.h>

namespace print_input_report {

zx_status_t PrintInputDescriptor(Printer* printer,
                                 fuchsia_input_report::InputDevice::SyncClient* client) {
  fuchsia_input_report::InputDevice::ResultOf::GetDescriptor result = client->GetDescriptor();
  if (result.status() != ZX_OK) {
    printer->Print("GetDescriptor FIDL call returned %s\n", zx_status_get_string(result.status()));
    return result.status();
  }

  printer->SetIndent(0);
  if (result->descriptor.has_mouse()) {
    if (result->descriptor.mouse().has_input()) {
      PrintMouseDesc(printer, result->descriptor.mouse().input());
    }
  }
  if (result->descriptor.has_sensor()) {
    if (result->descriptor.sensor().has_input()) {
      PrintSensorDesc(printer, result->descriptor.sensor().input());
    }
  }
  if (result->descriptor.has_touch()) {
    PrintTouchDesc(printer, result->descriptor.touch().input());
  }
  if (result->descriptor.has_keyboard()) {
    PrintKeyboardDesc(printer, result->descriptor.keyboard());
  }
  if (result->descriptor.has_consumer_control()) {
    PrintConsumerControlDesc(printer, result->descriptor.consumer_control());
  }
  return ZX_OK;
}

void PrintMouseDesc(Printer* printer,
                    const fuchsia_input_report::MouseInputDescriptor& mouse_desc) {
  printer->Print("Mouse Descriptor:\n");
  printer->IncreaseIndent();
  if (mouse_desc.has_movement_x()) {
    printer->Print("Movement X:\n");
    printer->PrintAxisIndented(mouse_desc.movement_x());
  }
  if (mouse_desc.has_movement_y()) {
    printer->Print("Movement Y:\n");
    printer->PrintAxisIndented(mouse_desc.movement_y());
  }
  if (mouse_desc.has_position_x()) {
    printer->Print("Position X:\n");
    printer->PrintAxisIndented(mouse_desc.position_x());
  }
  if (mouse_desc.has_position_y()) {
    printer->Print("Position Y:\n");
    printer->PrintAxisIndented(mouse_desc.position_y());
  }
  if (mouse_desc.has_buttons()) {
    for (uint8_t button : mouse_desc.buttons()) {
      printer->Print("Button: %d\n", button);
    }
  }
  printer->DecreaseIndent();
}

void PrintSensorDesc(Printer* printer,
                     const fuchsia_input_report::SensorInputDescriptor& sensor_desc) {
  printer->Print("Sensor Descriptor:\n");
  if (!sensor_desc.has_values()) {
    return;
  }

  printer->IncreaseIndent();
  for (size_t i = 0; i < sensor_desc.values().count(); i++) {
    printer->Print("Value %02d:\n", i);
    printer->IncreaseIndent();
    printer->Print("SensorType: %s\n", printer->SensorTypeToString(sensor_desc.values()[i].type));
    printer->PrintAxis(sensor_desc.values()[i].axis);
    printer->DecreaseIndent();
  }
  printer->DecreaseIndent();
}

void PrintTouchDesc(Printer* printer,
                    const fuchsia_input_report::TouchInputDescriptor& touch_desc) {
  printer->Print("Touch Descriptor:\n");
  printer->IncreaseIndent();
  if (touch_desc.has_touch_type()) {
    printer->Print("Touch Type: %s\n", printer->TouchTypeToString(touch_desc.touch_type()));
  }
  if (touch_desc.has_max_contacts()) {
    printer->Print("Max Contacts: %ld\n", touch_desc.max_contacts());
  }
  if (touch_desc.has_contacts()) {
    for (size_t i = 0; i < touch_desc.contacts().count(); i++) {
      const fuchsia_input_report::ContactInputDescriptor& contact = touch_desc.contacts()[i];

      printer->Print("Contact: %02d\n", i);
      printer->IncreaseIndent();

      if (contact.has_position_x()) {
        printer->Print("Position X:\n");
        printer->PrintAxisIndented(contact.position_x());
      }
      if (contact.has_position_y()) {
        printer->Print("Position Y:\n");
        printer->PrintAxisIndented(contact.position_y());
      }
      if (contact.has_pressure()) {
        printer->Print("Pressure:\n");
        printer->PrintAxisIndented(contact.pressure());
      }
      if (contact.has_contact_width()) {
        printer->Print("Contact Width:\n");
        printer->PrintAxisIndented(contact.contact_width());
      }
      if (contact.has_contact_height()) {
        printer->Print("Contact Height:\n");
        printer->PrintAxisIndented(contact.contact_height());
      }

      printer->DecreaseIndent();
    }
  }
  printer->DecreaseIndent();
}

void PrintKeyboardDesc(Printer* printer,
                       const fuchsia_input_report::KeyboardDescriptor& descriptor) {
  printer->Print("Keyboard Descriptor:\n");

  if (descriptor.has_input()) {
    const fuchsia_input_report::KeyboardInputDescriptor& input = descriptor.input();
    printer->Print("Input Report:\n");
    printer->IncreaseIndent();
    if (input.has_keys()) {
      for (size_t i = 0; i < input.keys().count(); i++) {
        printer->Print("Key: %8ld\n", input.keys()[i]);
      }
    }
    printer->DecreaseIndent();
  }
  if (descriptor.has_output()) {
    const fuchsia_input_report::KeyboardOutputDescriptor& output = descriptor.output();
    printer->Print("Output Report:\n");
    printer->IncreaseIndent();
    if (output.has_leds()) {
      for (size_t i = 0; i < output.leds().count(); i++) {
        printer->Print("Led: %s\n", Printer::LedTypeToString(output.leds()[i]));
      }
    }
    printer->DecreaseIndent();
  }
}

void PrintConsumerControlDesc(Printer* printer,
                              const fuchsia_input_report::ConsumerControlDescriptor& descriptor) {
  printer->Print("ConsumerControl Descriptor:\n");

  if (descriptor.has_input()) {
    const fuchsia_input_report::ConsumerControlInputDescriptor& input = descriptor.input();
    printer->Print("Input Report:\n");
    printer->IncreaseIndent();
    if (input.has_buttons()) {
      for (size_t i = 0; i < input.buttons().count(); i++) {
        printer->Print("Button: %16s\n",
                       Printer::ConsumerControlButtonToString(input.buttons()[i]));
      }
    }
    printer->DecreaseIndent();
  }
}

int PrintInputReport(Printer* printer, fuchsia_input_report::InputDevice::SyncClient* client,
                     size_t num_reads) {
  zx_status_t status;

  // Get the InputReportsReader.
  llcpp::fuchsia::input::report::InputReportsReader::SyncClient reader;
  {
    zx::channel token_server, token_client;
    status = zx::channel::create(0, &token_server, &token_client);
    if (status != ZX_OK) {
      return 1;
    }
    auto result = client->GetInputReportsReader(std::move(token_server));
    if (result.status() != ZX_OK) {
      return 1;
    }
    reader = llcpp::fuchsia::input::report::InputReportsReader::SyncClient(std::move(token_client));
  }

  while (num_reads--) {
    // Get the reports.
    auto result = reader.ReadInputReports();
    if (result.status() != ZX_OK) {
      printer->Print("GetReports FIDL call returned %s\n", zx_status_get_string(result.status()));
      return 1;
    }
    if (result->result.is_err()) {
      return 1;
    }

    auto& reports = result->result.response().reports;
    TRACE_DURATION("input", "print-input-report ReadReports");
    for (auto& report : reports) {
      printer->SetIndent(0);
      if (report.has_event_time()) {
        printer->Print("EventTime: 0x%016lx\n", report.event_time());
      }
      if (report.has_trace_id()) {
        TRACE_FLOW_END("input", "input_report", report.trace_id());
      }
      if (report.has_mouse()) {
        auto& mouse = report.mouse();
        PrintMouseInputReport(printer, mouse);
      }
      if (report.has_sensor()) {
        PrintSensorInputReport(printer, report.sensor());
      }
      if (report.has_touch()) {
        PrintTouchInputReport(printer, report.touch());
      }
      if (report.has_keyboard()) {
        PrintKeyboardInputReport(printer, report.keyboard());
      }
      if (report.has_consumer_control()) {
        PrintConsumerControlInputReport(printer, report.consumer_control());
      }
      printer->Print("\n");
    }
  }
  return 0;
}

void PrintMouseInputReport(Printer* printer,
                           const fuchsia_input_report::MouseInputReport& mouse_report) {
  if (mouse_report.has_movement_x()) {
    printer->Print("Movement x: %08ld\n", mouse_report.movement_x());
  }
  if (mouse_report.has_movement_y()) {
    printer->Print("Movement y: %08ld\n", mouse_report.movement_y());
  }
  if (mouse_report.has_position_x()) {
    printer->Print("Position x: %08ld\n", mouse_report.position_x());
  }
  if (mouse_report.has_position_y()) {
    printer->Print("Position y: %08ld\n", mouse_report.position_y());
  }
  if (mouse_report.has_scroll_v()) {
    printer->Print("Scroll v: %08ld\n", mouse_report.scroll_v());
  }
  if (mouse_report.has_pressed_buttons()) {
    for (uint8_t button : mouse_report.pressed_buttons()) {
      printer->Print("Button %02d pressed\n", button);
    }
  }
}

void PrintSensorInputReport(Printer* printer,
                            const fuchsia_input_report::SensorInputReport& sensor_report) {
  if (!sensor_report.has_values()) {
    return;
  }

  for (size_t i = 0; i < sensor_report.values().count(); i++) {
    printer->Print("Sensor[%02d]: %08d\n", i, sensor_report.values()[i]);
  }
}

void PrintTouchInputReport(Printer* printer,
                           const fuchsia_input_report::TouchInputReport& touch_report) {
  if (touch_report.has_contacts()) {
    for (size_t i = 0; i < touch_report.contacts().count(); i++) {
      const fuchsia_input_report::ContactInputReport& contact = touch_report.contacts()[i];

      if (contact.has_contact_id()) {
        printer->Print("Contact ID: %2ld\n", contact.contact_id());
      } else {
        printer->Print("Contact: %2d\n", i);
      }

      printer->IncreaseIndent();
      if (contact.has_position_x()) {
        printer->Print("Position X:     %08ld\n", contact.position_x());
      }
      if (contact.has_position_y()) {
        printer->Print("Position Y:     %08ld\n", contact.position_y());
      }
      if (contact.has_pressure()) {
        printer->Print("Pressure:       %08ld\n", contact.pressure());
      }
      if (contact.has_contact_width()) {
        printer->Print("Contact Width:  %08ld\n", contact.contact_width());
      }
      if (contact.has_contact_height()) {
        printer->Print("Contact Height: %08ld\n", contact.contact_height());
      }

      printer->DecreaseIndent();
    }
  }
}

void PrintKeyboardInputReport(Printer* printer,
                              const fuchsia_input_report::KeyboardInputReport& keyboard_report) {
  printer->Print("Keyboard Report\n");
  printer->IncreaseIndent();
  if (keyboard_report.has_pressed_keys()) {
    for (size_t i = 0; i < keyboard_report.pressed_keys().count(); i++) {
      printer->Print("Key: %8ld\n", keyboard_report.pressed_keys()[i]);
    }
    if (keyboard_report.pressed_keys().count() == 0) {
      printer->Print("No keys pressed\n");
    }
  }
  printer->DecreaseIndent();
}

void PrintConsumerControlInputReport(
    Printer* printer, const fuchsia_input_report::ConsumerControlInputReport& report) {
  printer->Print("ConsumerControl Report\n");
  printer->IncreaseIndent();
  if (report.has_pressed_buttons()) {
    for (size_t i = 0; i < report.pressed_buttons().count(); i++) {
      printer->Print("Button: %16s\n",
                     Printer::ConsumerControlButtonToString(report.pressed_buttons()[i]));
    }
  }
  printer->DecreaseIndent();
}

}  // namespace print_input_report
