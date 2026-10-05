// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge
// InnoEdge Scratch 3.0 / TurboWarp Custom Extension
// Dành cho học sinh và giáo viên lập trình Robot STEM, Kiosk thông minh & IoT thương mại.

(function(Scratch) {
    'use strict';

    if (!Scratch.extensions.unsandboxed) {
        // Cho phép chạy trong chế độ sandbox hoặc unsandboxed
    }

    class InnoEdgeExtension {
        constructor() {
            this.ws = null;
            this.connected = false;
            this.lastPaidAmount = 0;
            this.lastIntentId = 0;
            this.obstacleDistanceCm = 25.0;
            this.robotSpeed = 80;
        }

        getInfo() {
            return {
                id: 'innoedge',
                name: 'InnoEdge STEM Robot',
                color1: '#0052cc',
                color2: '#0747a6',
                color3: '#172b4d',
                blocks: [
                    {
                        opcode: 'whenPaid',
                        blockType: Scratch.BlockType.HAT,
                        text: '🟢 Khi nhận thanh toán VietQR [AMOUNT] đ',
                        arguments: {
                            AMOUNT: {
                                type: Scratch.ArgumentType.NUMBER,
                                defaultValue: 20000
                            }
                        }
                    },
                    {
                        opcode: 'whenRemoteCommand',
                        blockType: Scratch.BlockType.HAT,
                        text: '⚡ Khi nhận lệnh từ xa [COMMAND]',
                        arguments: {
                            COMMAND: {
                                type: Scratch.ArgumentType.STRING,
                                menu: 'commandMenu',
                                defaultValue: 'bot_move'
                            }
                        }
                    },
                    {
                        opcode: 'robotMove',
                        blockType: Scratch.BlockType.COMMAND,
                        text: '🚗 Robot [DIRECTION] tốc độ [SPEED]% trong [SEC] giây',
                        arguments: {
                            DIRECTION: {
                                type: Scratch.ArgumentType.STRING,
                                menu: 'directionMenu',
                                defaultValue: 'forward'
                            },
                            SPEED: {
                                type: Scratch.ArgumentType.NUMBER,
                                defaultValue: 80
                            },
                            SEC: {
                                type: Scratch.ArgumentType.NUMBER,
                                defaultValue: 1.5
                            }
                        }
                    },
                    {
                        opcode: 'robotStop',
                        blockType: Scratch.BlockType.COMMAND,
                        text: '🛑 Dừng Robot ngay lập tức'
                    },
                    {
                        opcode: 'setServoAngle',
                        blockType: Scratch.BlockType.COMMAND,
                        text: '🦾 Quay Servo [SERVO_ID] góc [ANGLE]°',
                        arguments: {
                            SERVO_ID: {
                                type: Scratch.ArgumentType.NUMBER,
                                defaultValue: 1
                            },
                            ANGLE: {
                                type: Scratch.ArgumentType.NUMBER,
                                defaultValue: 90
                            }
                        }
                    },
                    {
                        opcode: 'triggerRelay',
                        blockType: Scratch.BlockType.COMMAND,
                        text: '⚡ Kích Relay [CHANNEL] trong [SEC] giây',
                        arguments: {
                            CHANNEL: {
                                type: Scratch.ArgumentType.NUMBER,
                                defaultValue: 1
                            },
                            SEC: {
                                type: Scratch.ArgumentType.NUMBER,
                                defaultValue: 3
                            }
                        }
                    },
                    {
                        opcode: 'requestVietQR',
                        blockType: Scratch.BlockType.COMMAND,
                        text: '💳 Tạo mã VietQR [AMOUNT] đ cho món [ITEM]',
                        arguments: {
                            AMOUNT: {
                                type: Scratch.ArgumentType.NUMBER,
                                defaultValue: 25000
                            },
                            ITEM: {
                                type: Scratch.ArgumentType.STRING,
                                defaultValue: 'Giao hàng Robot'
                            }
                        }
                    },
                    {
                        opcode: 'getDistance',
                        blockType: Scratch.BlockType.REPORTER,
                        text: '📏 Khoảng cách vật cản (cm)'
                    },
                    {
                        opcode: 'isObstacleNear',
                        blockType: Scratch.BlockType.BOOLEAN,
                        text: '⚠️ Có vật cản phía trước < [DIST] cm?',
                        arguments: {
                            DIST: {
                                type: Scratch.ArgumentType.NUMBER,
                                defaultValue: 15
                            }
                        }
                    },
                    {
                        opcode: 'getLastPaid',
                        blockType: Scratch.BlockType.REPORTER,
                        text: '💰 Số tiền vừa nhận (đ)'
                    },
                    {
                        opcode: 'robotSpeak',
                        blockType: Scratch.BlockType.COMMAND,
                        text: '🗣️ Robot nói: [TEXT]',
                        arguments: {
                            TEXT: {
                                type: Scratch.ArgumentType.STRING,
                                defaultValue: 'Cảm ơn bạn! Đơn hàng của bạn đây.'
                            }
                        }
                    }
                ],
                menus: {
                    directionMenu: {
                        acceptReporters: false,
                        items: [
                            { text: 'Tiến lên ⬆️', value: 'forward' },
                            { text: 'Lùi lại ⬇️', value: 'backward' },
                            { text: 'Rẽ trái ⬅️', value: 'left' },
                            { text: 'Rẽ phải ➡️', value: 'right' }
                        ]
                    },
                    commandMenu: {
                        acceptReporters: false,
                        items: [
                            { text: 'bot_move (Di chuyển)', value: 'bot_move' },
                            { text: 'bot_deliver (Giao hàng)', value: 'bot_deliver' },
                            { text: 'dispense (Nhả đồ)', value: 'dispense' },
                            { text: 'reboot (Khởi động lại)', value: 'reboot' }
                        ]
                    }
                }
            };
        }

        // Logic thực thi các khối Scratch
        robotMove(args) {
            console.log(`[InnoEdge Bot] Move: ${args.DIRECTION} at ${args.SPEED}% for ${args.SEC}s`);
            return new Promise((resolve) => setTimeout(resolve, args.SEC * 1000));
        }

        robotStop() {
            console.log('[InnoEdge Bot] Stop motors');
        }

        setServoAngle(args) {
            console.log(`[InnoEdge Bot] Servo ${args.SERVO_ID} -> ${args.ANGLE} deg`);
        }

        triggerRelay(args) {
            console.log(`[InnoEdge Bot] Relay ${args.CHANNEL} ON for ${args.SEC}s`);
        }

        requestVietQR(args) {
            console.log(`[InnoEdge Bot] Request VietQR: ${args.AMOUNT} VND for "${args.ITEM}"`);
        }

        getDistance() {
            return this.obstacleDistanceCm;
        }

        isObstacleNear(args) {
            return this.obstacleDistanceCm < args.DIST;
        }

        getLastPaid() {
            return this.lastPaidAmount;
        }

        robotSpeak(args) {
            console.log(`[InnoEdge Bot] Speak: "${args.TEXT}"`);
        }
    }

    if (typeof Scratch !== 'undefined' && Scratch.extensions) {
        Scratch.extensions.register(new InnoEdgeExtension());
    } else if (typeof window !== 'undefined') {
        window.InnoEdgeScratchExtension = InnoEdgeExtension;
    }
})(typeof Scratch !== 'undefined' ? Scratch : {});
