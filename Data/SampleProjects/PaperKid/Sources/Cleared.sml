<screen mode="modal" transition="fade" default-focus="continue-btn">

  <Panel class="dim">
  <Flex direction="vertical" justify="center" align="center" padding="32">

    <Panel padding="40" class="card-good">
      <Flex direction="vertical" align="center" spacing="8">
        <Label id="cleared-title" font-family="Lilita One" text="Block cleared!" font-size="56" class="headline-good"/>
        <Label id="cleared-summary" text="" font-size="24" class="body"/>
        <Spacer spacer-height="24"/>
        <Flex direction="vertical" spacing="10" width="300">
          <Button id="continue-btn" text="Continue" height="52" class="primary" font-size="24"/>
        </Flex>
      </Flex>
    </Panel>

  </Flex>
  </Panel>

</screen>
