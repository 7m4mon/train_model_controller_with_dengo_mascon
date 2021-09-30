# 電車でGO!マスコンで鉄道模型を運転しよう！
7M4MON, 2021.Dec.1  

### はじめに
[『電車でGO!コントローラでTSマスコンをエミュレートしてBVEで遊ぶ。』](https://github.com/7m4mon/dengo_ts_emu) でArduinoから電車でGO！のコントローラを扱うことができましたので、次は、電車でGO！のコントローラを使って、鉄道模型を運転します。

YouTube Video  
[![](https://img.youtube.com/vi/ahtvqu5uVL8/0.jpg)](https://www.youtube.com/watch?v=ahtvqu5uVL8)


### ハードウェア
* 電源は DC12V のACアダプタを使用します。Nゲージなら 2A程度の電流容量があれば十分です。
* フィーダーはネジ式の端子台に接続します。モータードライバICにサーマルシャットダウン機能がありますが、念の為、ポリヒューズを付けています。
* モータードライバは 東芝 TA8428K です。気休めに放熱板をつけていて、Nゲージならほんのり温くなる程度です。
* マイコンは Arduino Nano (ATMega328P) です。プログラム開発は、Arduino IDEを使用しています。
* 表示部はTM1637 の4桁7セグLED と WS2812B の 12LEDリングです。コントローラーの懐中時計用の窪みに設置できるように、直径50mm程度のリングをチョイスしました。[表示器は時計にもなります。](https://github.com/7m4mon/Neopixel_Ring_TMDSPL_Clock)
* 走行スピードはPWMで制御していますが、Duty比が小さいと走り出しません。そのため、最小Duty比を設定するための半固定抵抗と設定モード切替スイッチを設けています。設定可能範囲は０～２５％です。電気機関車はDuty比を上げる傾向にあるなど、車両により若干異なるようです。
* 各ボタン（Start,Select,A,B,C）を押すと、警笛音や発車メロディ等、５種類の音がなります。再生はJQ6500のGPIO制御ですが、マイコンポートでのHi-Zはうまく動作しなかったので、各ポートにデジトラ(RN1201)を追加しています。
* 他、接続図などは下ブロック図のようになっています。  

<img src="https://github.com/7m4mon/train_model_controller_with_dengo_mascon/blob/main/model_cont_block.png" alt="model_cont_block" title="">  


### ソフトウェア
* 電車でGOコントローラの制御部は、[『電車でGO!コントローラでTSマスコンをエミュレートしてBVEで遊ぶ。』](https://github.com/7m4mon/dengo_ts_emu) からの流用です。[Gyokimae氏の自作ライブラリ](http://pspunch.com/pd/article/arduino_lib_gpsx.html)を使用し、[KeKe115sさんの電GOコントローラーをArduinoで読み取るサンプル](https://github.com/KeKe115/Densya_de_go)を参考にしています。
* 20ms毎にタイマ割り込みが入り、コントローラの状態を読み出し、処理を行います。すべての処理をこのタイマ割り込みで行い、メインループに処理はありません。
* マスコンハンドルの位置を読みだしたら、位置に応じて加減速計算処理を行い、速度を決定します。
この計算は2048段階で行っていますが、analogWrite は256段階ですのでPWMセット時には３ビット右シフトをしています。
* 20msのタイマ割り込みは表示や計算で処理を分割し、５つに分散させています。そのため、実際にPWMのパルス幅を更新するのは100ms毎です。
* 電源投入時はレバーサの状態は「切」になっています。スタートボタン長押しで 中立→前進→中立→後退 に状態遷移します。
* セレクトボタン長押しでパワーパックモードへ切り替えます。パワーパックモードはマスコン位置１～５でダイレクトにスピードを決定します。
* PWMのセット範囲は０～２５５ですが、表示範囲は０～１００なので、２５５→１００となるように、固定小数点演算をしています。具体的には、２５５＊２０１÷２＾９＝５１２５５÷５１２＝１００（小数点以下切り捨て）です。
* リングLEDはスピードの段階を８色で表現しています。満了したら白にして、次のLEDを点灯して色を変えていきます。速度ゼロのときは赤となります。
* リングLEDの最下はレバーサの状態を表し、停止（赤）／前進（緑）／後退（青）となっています。


### 制作例
<img src="https://github.com/7m4mon/train_model_controller_with_dengo_mascon/blob/main/dengo_model_1.jpg" alt="" title="">  
<img src="https://github.com/7m4mon/train_model_controller_with_dengo_mascon/blob/main/dengo_model_2.jpg" alt="" title="">  
<img src="https://github.com/7m4mon/train_model_controller_with_dengo_mascon/blob/main/dengo_model_3.jpg" alt="" title="">  
