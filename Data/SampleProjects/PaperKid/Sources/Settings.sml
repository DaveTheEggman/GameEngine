<screen mode="modal" transition="fade" default-focus="master-slider">

  <Panel style="background: rgba(8, 10, 18, 0.55);">
  <Flex direction="vertical" justify="center" align="center" padding="32">

    <Panel padding="40"
           style="background: rounded-rect(rgb(24, 26, 36), radius=14, border-width=2, border=rgb(90, 104, 130));">
      <Flex direction="vertical" align="center">
        <Label id="settings-title" font-family="Lilita One" text="Settings" font-size="56" style="text-color: rgb(225, 230, 240);"/>
        <Spacer spacer-height="22"/>

        <Flex direction="vertical" spacing="16">
          <Flex direction="horizontal" align="center" spacing="16">
            <Label text="Master" font-size="22" width="110" style="text-color: rgb(225, 230, 240);"/>
            <Slider id="master-slider" min="0" max="1" step="0.05" width="280" height="30"/>
            <Label id="master-value" text="100%" font-size="20" width="64" style="text-color: rgb(190, 198, 214);"/>
          </Flex>
          <Flex direction="horizontal" align="center" spacing="16">
            <Label text="Music" font-size="22" width="110" style="text-color: rgb(225, 230, 240);"/>
            <Slider id="music-slider" min="0" max="1" step="0.05" width="280" height="30"/>
            <Label id="music-value" text="100%" font-size="20" width="64" style="text-color: rgb(190, 198, 214);"/>
          </Flex>
          <Flex direction="horizontal" align="center" spacing="16">
            <Label text="Effects" font-size="22" width="110" style="text-color: rgb(225, 230, 240);"/>
            <Slider id="effects-slider" min="0" max="1" step="0.05" width="280" height="30"/>
            <Label id="effects-value" text="100%" font-size="20" width="64" style="text-color: rgb(190, 198, 214);"/>
          </Flex>
        </Flex>

        <Spacer spacer-height="28"/>
        <Button id="settings-back-btn" text="Back" width="240" height="52" class="primary" font-size="24"/>
        <Spacer spacer-height="14"/>
        <Label text="Left and right to adjust, Esc or Start to go back" font-size="18" style="text-color: rgb(190, 198, 214);"/>
      </Flex>
    </Panel>

  </Flex>
  </Panel>

</screen>
